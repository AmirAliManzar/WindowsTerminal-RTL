# LOCAL BUILD ADAPTATION (WindowsTerminal-RTL): a plain static triplet whose
# platform toolset WT_VCPKG_TOOLSET can pin.
#
# This file does not exist at v1.24.11911.0; upstream added the overlay triplets
# later, when it pinned v145 (microsoft/terminal#20225). Here pre.props still
# pins v143, so the toolset the dependencies are built with has to be
# controllable too, or a runner whose newest Visual Studio is newer than v143
# builds the dependencies against a different toolset than the terminal itself.
#
# WT_VCPKG_TOOLSET is only consulted when set; when it is not, vcpkg picks the
# latest toolset on the machine, which is what upstream wanted and what a local
# build on a matching Visual Studio already gives you.
if(DEFINED ENV{WT_VCPKG_TOOLSET} AND NOT "$ENV{WT_VCPKG_TOOLSET}" STREQUAL "")
  set(VCPKG_PLATFORM_TOOLSET $ENV{WT_VCPKG_TOOLSET})
  message(STATUS "wt-rtl: VCPKG_PLATFORM_TOOLSET pinned to $ENV{WT_VCPKG_TOOLSET}")
endif()
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
