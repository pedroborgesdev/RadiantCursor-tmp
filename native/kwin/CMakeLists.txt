cmake_minimum_required(VERSION 3.20)

project(radiantcursor-effect VERSION 1.0.0 LANGUAGES CXX)

set(KF6_MIN_VERSION "6.0.0")

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(FeatureSummary)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6CoreAddons ${KF6_MIN_VERSION} REQUIRED)
find_package(KF6Config ${KF6_MIN_VERSION} REQUIRED)
find_package(KWin REQUIRED COMPONENTS kwin)

kcoreaddons_add_plugin(radiantcursor
    SOURCES
        main.cpp
        radiantcursoreffect.cpp
        radiantcursoreffect.h
        engine/effectengine.cpp
        engine/effectengine.h
    INSTALL_NAMESPACE "kwin/effects/plugins"
)

target_compile_features(radiantcursor PRIVATE cxx_std_20)
target_link_libraries(radiantcursor PRIVATE
    KWin::kwin
    KF6::ConfigCore
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
