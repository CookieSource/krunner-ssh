set(SSHHELPER_SRCS
    sshhelper.cpp
    sshhelper_common.cpp
    sshdiscovery.cpp
    sshhelper.json
)

set_source_files_properties(sshhelper.json PROPERTIES HEADER_FILE_ONLY ON)

kcoreaddons_add_plugin(krunner_sshhelper
    INSTALL_NAMESPACE "kf6/krunner"
    SOURCES ${SSHHELPER_SRCS}
)

target_link_libraries(krunner_sshhelper
    Qt6::Core
    Qt6::Network
    KF6::CoreAddons
    KF6::I18n
    KF6::ConfigCore
    KF6::Runner
)

target_compile_definitions(krunner_sshhelper PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII TRANSLATION_DOMAIN="plasma_runner_sshhelper")

kcoreaddons_add_plugin(kcm_krunner_sshhelper
    INSTALL_NAMESPACE "kf6/krunner/kcms"
    SOURCES
        sshhelper_common.cpp
        sshdiscovery.cpp
        kcms/entriesmodel.cpp
        kcms/manualentrydialog.cpp
        kcms/sshhelperkcm.cpp
)

target_link_libraries(kcm_krunner_sshhelper
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    KF6::CoreAddons
    KF6::I18n
    KF6::ConfigCore
    KF6::KCMUtils
)

target_compile_definitions(kcm_krunner_sshhelper PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII TRANSLATION_DOMAIN="plasma_runner_sshhelper")
