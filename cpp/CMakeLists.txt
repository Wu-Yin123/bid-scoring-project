cmake_minimum_required(VERSION 3.21)
project(RegulationBatchProcessor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_executable(RegulationBatchProcessor
    main.cpp
    models.cpp
    models.h
    rule_engine.cpp
    rule_engine.h
    document_processor.cpp
    document_processor.h
    batch_worker.cpp
    batch_worker.h
    mainwindow.cpp
    mainwindow.h
    D:/Code/Codex/标书评分项目/ui/mainwindow.ui
    rule_library_dialog.cpp
    rule_library_dialog.h
    D:/Code/Codex/标书评分项目/ui/rulelibrarydialog.ui
    rule_library.cpp
    rule_library.h
    regulation_package_exporter.cpp
    regulation_package_exporter.h
    review_document.cpp
    review_document.h
    review_dialog.cpp
    review_dialog.h
    D:/Code/Codex/标书评分项目/ui/reviewdialog.ui
)

target_link_libraries(RegulationBatchProcessor PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

set_property(TARGET RegulationBatchProcessor PROPERTY AUTOUIC_SEARCH_PATHS
    "D:/Code/Codex/标书评分项目/ui")

target_compile_definitions(RegulationBatchProcessor PRIVATE QT_DISABLE_DEPRECATED_BEFORE=0x060500)

set_target_properties(RegulationBatchProcessor PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

option(BUILD_REVIEW_TESTS "Build the review workflow tests" OFF)
if(BUILD_REVIEW_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    set(REGULATION_TEST_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tests" CACHE PATH "Directory containing review tests")
    enable_testing()
    qt_add_executable(ReviewDocumentTest
        "${REGULATION_TEST_SOURCE_DIR}/review_document_test.cpp"
        review_document.cpp review_document.h)
    target_include_directories(ReviewDocumentTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ReviewDocumentTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME ReviewDocumentTest COMMAND ReviewDocumentTest)
    qt_add_executable(ReviewDialogTest
        "${REGULATION_TEST_SOURCE_DIR}/review_dialog_test.cpp"
        review_document.cpp review_document.h review_dialog.cpp review_dialog.h)
    target_include_directories(ReviewDialogTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ReviewDialogTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
    add_test(NAME ReviewDialogTest COMMAND ReviewDialogTest)
    set_tests_properties(ReviewDialogTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
