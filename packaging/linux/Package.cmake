# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Lúcio de Araújo
install(TARGETS risco_app RUNTIME DESTINATION bin)
install(FILES packaging/linux/risco.desktop DESTINATION share/applications)
install(FILES resources/icons/icon-512.png
    DESTINATION share/icons/hicolor/512x512/apps RENAME risco.png)
install(FILES LICENSE CREDITOS_E_LICENCAS.md packaging/README.md
    packaging/NOTAS_DE_VERSAO.md
    resources/fonts/LICENSE-DejaVu.txt third_party/ayumi/README-RISCO.md
    "${RISCO_JUCE_SOURCE_DIR}/JUCE.spdx.json" DESTINATION share/doc/risco)
install(FILES third_party/ayumi/LICENSE
    DESTINATION share/doc/risco RENAME LICENSE-Ayumi.txt)
install(FILES "${RISCO_JUCE_SOURCE_DIR}/LICENSE.md"
    DESTINATION share/doc/risco RENAME LICENSE-JUCE.md)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PACKAGE_NAME "risco")
set(CPACK_PACKAGE_VENDOR "RASGO")
set(CPACK_PACKAGE_CONTACT "Lúcio de Araújo <rasgo.instruments@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Risco, instrumento de composição generativa")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "risco-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# JUCE resolves these X11 libraries at runtime, so dpkg-shlibdeps cannot see them.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libx11-6, libxext6, libxcursor1, libxinerama1, libxrender1, libxrandr2")
include(CPack)
