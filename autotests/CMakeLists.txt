add_definitions( -DKDESRCDIR="${CMAKE_CURRENT_SOURCE_DIR}/" )

include_directories(${CMAKE_CURRENT_BINARY_DIR}/..)

macro(add_conf_unittest _source)
  set(_test ${_source} ../part/editdrawingtooldialog.cpp)
  get_filename_component(_name ${_source} NAME_WE)
  add_executable( ${_name} ${_test} )
  add_test( NAME ${_name} COMMAND ${_name} )
  ecm_mark_as_test(${_name})
  target_link_libraries( ${_name}
    Qt6::Test
    Qt6::Widgets
    Qt6::Xml
    KF6::I18n
    KF6::Completion
    KF6::WidgetsAddons
  )
endmacro ()

add_conf_unittest(editdrawingtooldialogtest.cpp)

ecm_add_test(shelltest.cpp ../shell/shellutils.cpp
    TEST_NAME "shelltest"
    LINK_LIBRARIES Qt6::Test okularcore
)

if(Poppler_Qt6_FOUND)
    if (BUILD_DESKTOP)
        ecm_add_test(parttest.cpp closedialoghelper.cpp
            TEST_NAME "parttest"
            LINK_LIBRARIES Qt6::Widgets Qt6::Test Qt6::Xml okularcore okularpart
        )
    endif()

    ecm_add_test(visibilitytest.cpp
        TEST_NAME "visibilitytest"
        LINK_LIBRARIES Qt6::Widgets Qt6::Test okularcore
    )

    ecm_add_test(jsfunctionstest.cpp
        TEST_NAME "jsfunctionstest"
        LINK_LIBRARIES Qt6::Widgets Qt6::Test okularcore
    )

    ecm_add_test(formattest.cpp
        TEST_NAME "formattest"
        LINK_LIBRARIES Qt6::Widgets Qt6::Test okularcore
    )

    ecm_add_test(keystroketest.cpp
        TEST_NAME "keystroketest"
        LINK_LIBRARIES Qt6::Widgets Qt6::Test okularcore
    )

    ecm_add_test(signunsignedfieldtest.cpp
        TEST_NAME "signunsignedfieldtest"
        LINK_LIBRARIES Qt6::Widgets Qt6::Test okularcore
    )
endif()

ecm_add_test(suggestedfilenametest.cpp
    TEST_NAME "suggestedfilenametest"
    LINK_LIBRARIES Qt6::Test okularcore KF6::I18n
)

ecm_add_test(documenttest.cpp
    TEST_NAME "documenttest"
    LINK_LIBRARIES Qt6::Widgets Qt6::Test Qt6::Xml okularcore KF6::ThreadWeaver
)

ecm_add_test(searchtest.cpp
    TEST_NAME "searchtest"
    LINK_LIBRARIES Qt6::Widgets Qt6::Test Qt6::Xml okularcore
)

ecm_add_test(annotationstest.cpp
    TEST_NAME "annotationstest"
    LINK_LIBRARIES Qt6::Widgets Qt6::Test Qt6::Xml okularcore
)

if(BUILD_DESKTOP)
    ecm_add_test(annotationclipboardtest.cpp
        TEST_NAME "annotationclipboardtest"
        LINK_LIBRARIES Qt6::Widgets Qt6::Test Qt6::Xml okularcore okularpart
    )
endif()

ecm_add_test(urldetecttest.cpp
    TEST_NAME "urldetecttest"
    LINK_LIBRARIES Qt6::Widgets Qt6::Test Qt6::Xml KF6::CoreAddons
)

ecm_add_test(editannotationcontentstest.cpp testingutils.cpp
    TEST_NAME "editannotationcontentstest"
    LINK_LIBRARIES Qt6::Widgets Qt6::Test Qt6::Xml okularcore
)

ecm_add_test(addremoveannotationtest.cpp testingutils.cpp
    TEST_NAME "addremoveannotationtest"
    LINK_LIBRARIES Qt6::Widgets Qt6::Test Qt6::Xml okularcore
)

ecm_add_test(translateannotationtest.cpp testingutils.cpp
    TEST_NAME "translateannotationtest"
    LINK_LIBRARIES Qt6::Widgets Qt6::Test Qt6::Xml okularcore
)

ecm_add_test(modifyannotationpropertiestest.cpp testingutils.cpp
    TEST_NAME "modifyannotationpropertiestest"
    LINK_LIBRARIES Qt6::Widgets Qt6::Test okularcore
)

ecm_add_test(editformstest.cpp
    TEST_NAME "editformstest"
    LINK_LIBRARIES Qt6::Widgets Qt6::Test okularcore
)

ecm_add_test(calculatetexttest.cpp
    TEST_NAME "calculatetexttest"
    LINK_LIBRARIES Qt6::Widgets Qt6::Test okularcore
)

ecm_add_test(check_distinguished_name_parser.cpp
    TEST_NAME "distinguishednameparser"
    LINK_LIBRARIES Qt6::Test)

if(BUILD_DESKTOP)
    ecm_add_test(mainshelltest.cpp ../shell/okular_main.cpp ../shell/shellutils.cpp ../shell/shell.cpp ../shell/welcomescreen.cpp ../gui/recentitemsmodel.cpp closedialoghelper.cpp
        TEST_NAME "mainshelltest"
        LINK_LIBRARIES Qt6::Test okularpart okularcore
    )
    target_compile_definitions(mainshelltest PRIVATE OKULAR_BINARY="$<TARGET_FILE:okular>")
    if(WIN32)
        if(MSVC)
            target_sources(mainshelltest PRIVATE ../shell/okular.manifest)
        else()
            set(OKULAR_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../shell/okular.manifest")
            configure_file(../shell/okular_manifest.rc.in mainshell_manifest.rc @ONLY)
            target_sources(mainshelltest PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/mainshell_manifest.rc)
        endif()
        if(TARGET okularGenerator_epub)
            target_compile_definitions(mainshelltest PRIVATE OKULAR_TEST_EPUB_LONG_PATH)
        endif()
    endif()

    if (HAVE_X11)
        target_link_libraries(mainshelltest Qt6::GuiPrivate)
    endif()
endif()

if(BUILD_DESKTOP)
    ecm_add_test(annotationtoolbartest.cpp ../shell/okular_main.cpp ../shell/shellutils.cpp ../shell/shell.cpp ../shell/welcomescreen.cpp ../gui/recentitemsmodel.cpp closedialoghelper.cpp ../shell/welcomescreen.ui
        TEST_NAME "annotationtoolbartest"
        LINK_LIBRARIES Qt6::Test okularpart
    )

    if (HAVE_X11)
        target_link_libraries(annotationtoolbartest Qt6::GuiPrivate)
    endif()
endif()

ecm_add_test(generatorstest.cpp
    TEST_NAME "generatorstest"
    LINK_LIBRARIES Qt6::Test KF6::CoreAddons okularcore
)
target_compile_definitions(generatorstest PRIVATE GENERATORS_BUILD_DIR="${CMAKE_BINARY_DIR}/generators")

ecm_add_test(signatureformtest.cpp
    TEST_NAME "signatureformtest"
    LINK_LIBRARIES Qt6::Test okularcore
)

find_package(Discount)

if(Discount_FOUND)
    ecm_add_test(markdowntest.cpp ../generators/markdown/converter.cpp
        TEST_NAME "markdowntest"
        LINK_LIBRARIES Qt6::Test okularcore KF6::I18n PkgConfig::Discount
    )
endif()

ecm_add_test(toggleactionmenutest.cpp ../part/toggleactionmenu.cpp
    TEST_NAME "toggleactionmenutest"
    LINK_LIBRARIES Qt6::Test KF6::WidgetsAddons
)

ecm_add_test(securitybudgettest.cpp
    TEST_NAME "securitybudgettest"
    LINK_LIBRARIES Qt6::Test KF6::Archive
)

if(Poppler_Qt6_FOUND AND TARGET okularGenerator_fb AND TARGET okularGenerator_epub AND TARGET okularGenerator_xps)
    ecm_add_test(securitydocumenttest.cpp ../generators/fictionbook/document.cpp ../generators/fictionbook/converter.cpp ../part/latexrenderer.cpp ../gui/debug_ui.cpp
        TEST_NAME "securitydocumenttest"
        LINK_LIBRARIES Qt6::Test okularcore KF6::Archive KF6::I18n KF6::CoreAddons Poppler::Qt6
    )
    add_dependencies(securitydocumenttest okularGenerator_poppler okularGenerator_fb okularGenerator_epub okularGenerator_xps)
    if(TARGET okularGenerator_tiff)
        add_dependencies(securitydocumenttest okularGenerator_tiff)
        target_compile_definitions(securitydocumenttest PRIVATE OKULAR_SECURITY_TIFF)
    endif()
    if(TARGET okularGenerator_ghostview)
        add_dependencies(securitydocumenttest okularGenerator_ghostview)
        target_compile_definitions(securitydocumenttest PRIVATE OKULAR_SECURITY_SPECTRE)
    endif()
    set_tests_properties(securitydocumenttest PROPERTIES TIMEOUT 30)
endif()
set_tests_properties(securitybudgettest PROPERTIES TIMEOUT 30)

ecm_add_test(textsecuritytest.cpp ../generators/txt/document.cpp ../generators/txt/converter.cpp
    TEST_NAME "textsecuritytest"
    LINK_LIBRARIES Qt6::Test okularcore
)
