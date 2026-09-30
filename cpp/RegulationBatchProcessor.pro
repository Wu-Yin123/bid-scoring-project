QT += core gui widgets
CONFIG += c++17 windows
TEMPLATE = app
TARGET = RegulationBatchProcessor

SOURCES += \
    main.cpp \
    models.cpp \
    rule_engine.cpp \
    document_processor.cpp \
    batch_worker.cpp \
    mainwindow.cpp \
    rule_library_dialog.cpp \
    rule_library.cpp \
    regulation_package_exporter.cpp \
    review_document.cpp \
    review_dialog.cpp

HEADERS += \
    models.h \
    rule_engine.h \
    document_processor.h \
    batch_worker.h \
    mainwindow.h \
    rule_library_dialog.h \
    rule_library.h \
    regulation_package_exporter.h \
    review_document.h \
    review_dialog.h

FORMS += \
    D:/Code/Codex/标书评分项目/ui/mainwindow.ui \
    D:/Code/Codex/标书评分项目/ui/reviewdialog.ui \
    D:/Code/Codex/标书评分项目/ui/rulelibrarydialog.ui

DESTDIR = $$PWD/bin
