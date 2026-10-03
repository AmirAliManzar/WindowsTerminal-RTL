#!/usr/bin/env python3
r"""Regenerate WindowsTerminal.slnf from OpenConsole.slnx.

A solution filter that is written by hand goes stale the moment the dependency
graph changes, and the failure it causes is far away from the cause. This builds
it instead, from the graph that is already in the repo:

  * ProjectReference closure from the exe we actually want, so ordering is
    correct by construction.
  * <BuildDependency> for every included project, which is how the .slnx declares
    "build this one first" without a project reference. Header generators live
    here: dropping src/host/proxy/Host.Proxy.vcxproj makes every consumer of
    ITerminalHandoff.h fail with C1083, and nothing in the project file hints at
    why.

Excluded on purpose:

  *.wapproj  the MSIX packaging project; needs the full UWP toolchain and builds
             the opposite of what we ship (we want a portable folder).
  *.csproj   need a .NET SDK restore this build does not have.
  tests     not needed to produce WindowsTerminal.exe, and they pull in Taef.

Usage:  python dep\make-solution-filter.py
"""

import json
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SLNX = os.path.join(ROOT, "OpenConsole.slnx")
SLN = os.path.join(ROOT, "OpenConsole.sln")
OUT = os.path.join(ROOT, "WindowsTerminal.slnf")

# Where the closure starts.
ROOTS = ["src/cascadia/WindowsTerminal/WindowsTerminal.vcxproj"]

# Never included, whatever the graph says.
EXCLUDE_EXT = (".wapproj", ".csproj", ".vcxproj.filters")
EXCLUDE_SUBSTR = ("test", "Test", "unittest", "unittest")

PROJECT_REF = re.compile(r'<ProjectReference\s+Include="([^"]+)"')

# The classic .sln format: one "Project(...)" line per project, the path in the
# third field, and dependencies declared in a nested "ProjectSection(...)" block
# rather than in the project element itself. microsoft/terminal moved to the
# XML .slnx form after v1.24.11911.0, so a checkout of the release tag only has
# the old one.
SLN_PROJECT = re.compile(r'^Project\("[^"]*"\)\s*=\s*"([^"]*)",\s*"([^"]*)",\s*"\{([^}]*)\}"')


def norm(path):
    """Solution paths use '/', project files use a mix; compare on one form."""
    return path.replace("\\", "/").lstrip("./")


def read_slnx():
    """Map project path -> list of <BuildDependency> paths, and the project set."""
    tree = ET.parse(SLNX)
    deps, known = {}, set()
    for project in tree.iter("Project"):
        path = norm(project.get("Path", ""))
        if not path:
            continue
        known.add(path)
        deps[path] = [
            norm(d.get("Project", ""))
            for d in project.findall("BuildDependency")
            if d.get("Project")
        ]
    return deps, known


def read_sln():
    """The same shape as read_slnx, from the classic text solution format.

    A .sln declares build order with a ProjectSection whose type is
    ProjectDependencies, listing the GUIDs a project must wait for. GUIDs are
    useless without a lookup table, so this maps each GUID to its path first and
    then resolves the edges to paths.
    """
    with open(SLN, "r", encoding="utf-8-sig", errors="replace") as fh:
        text = fh.read()

    guid_to_path = {}
    order = []
    for line in text.splitlines():
        match = SLN_PROJECT.match(line.strip())
        if not match:
            continue
        _name, path, guid = match.group(1), match.group(2), match.group(3)
        path = norm(path)
        guid_to_path[guid.lower()] = path
        order.append(path)

    deps, known = {}, set()
    for path in order:
        known.add(path)
        deps[path] = []

    # Re-scan for the dependency sections, which sit inside the project blocks.
    current = None
    in_deps = False
    for line in text.splitlines():
        stripped = line.strip()
        match = SLN_PROJECT.match(stripped)
        if match:
            current = norm(match.group(2))
            in_deps = False
            continue
        if "ProjectSection(ProjectDependencies)" in stripped:
            in_deps = True
            continue
        if "EndProjectSection" in stripped:
            in_deps = False
            continue
        if in_deps and current and "=" in stripped:
            guid = stripped.split("=", 1)[0].strip().strip("{}").lower()
            dep = guid_to_path.get(guid)
            if dep:
                deps.setdefault(current, []).append(dep)

    return deps, known


def read_solution():
    """Prefer the XML form; fall back to the classic one on older refs."""
    if os.path.isfile(SLNX):
        return read_slnx(), "OpenConsole.slnx"
    if os.path.isfile(SLN):
        return read_sln(), "OpenConsole.sln"
    return None, None


def project_references(rel_path):
    """ProjectReference targets from a .vcxproj, with $(...) macros dropped."""
    full = os.path.join(ROOT, rel_path.replace("/", os.sep))
    if not os.path.isfile(full):
        return []
    with open(full, "r", encoding="utf-8-sig", errors="replace") as fh:
        text = fh.read()
    out = []
    for match in PROJECT_REF.finditer(text):
        target = match.group(1).replace("\\", "/")
        if "$(" in target:
            # e.g. $(OpenConsoleDir)src\types\lib\types.vcxproj
            tail = target.split(")", 1)[1] if ")" in target else target
            tail = tail.lstrip("/\\")
            if tail:
                out.append(norm(tail))
    return out


def wanted(path):
    if path.endswith(EXCLUDE_EXT):
        return False
    base = os.path.basename(path)
    for bad in EXCLUDE_SUBSTR:
        if bad in base:
            return False
    return True


def main():
    solution, name = read_solution()
    if solution is None:
        sys.exit("neither OpenConsole.slnx nor OpenConsole.sln found next to this script")

    deps, known = solution

    included, missing = set(), []

    def add(path):
        path = norm(path)
        if not wanted(path):
            return
        if path in included:
            return
        if not os.path.isfile(os.path.join(ROOT, path.replace("/", os.sep))):
            missing.append(path)
            return
        included.add(path)
        # Build dependencies first: they generate headers others include.
        for dep in deps.get(path, []):
            add(dep)
        for ref in project_references(path):
            add(ref)

    for root in ROOTS:
        add(root)

    if missing:
        print("warning: referenced but not on disk:")
        for m in sorted(set(missing)):
            print("   " + m)

    unknown = [p for p in included if p not in known]
    if unknown:
        print("warning: included but not in {}:".format(name))
        for u in sorted(unknown):
            print("   " + u)

    # A .slnf lists projects with BACKSLASHES, while the .slnx it filters uses
    # forward slashes. Writing the .slnx form into the filter gives
    #   MSB4025: Solution filter file ... includes project "src/audio/..." that is
    #   not in the solution file ...
    # because MSBuild compares the two verbatim. Convert on the way out only;
    # everything above works in one normalised form.
    listing = json.dumps(
        {"solution": {"path": name,
                      "projects": sorted((p.replace("/", "\\") for p in included),
                                        key=str.lower)}},
        indent=2,
    )
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(listing + "\n")

    print("wrote {} with {} projects (from {})".format(os.path.basename(OUT), len(included), name))
    for p in sorted(included, key=str.lower):
        print("   " + p)


if __name__ == "__main__":
    main()