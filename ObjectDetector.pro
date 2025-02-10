QT       += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
CONFIG += c++17
DEFINES += PROJECT_PATH=\"$$PWD\"

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    detectionworker.cpp \
    gstreamerrtsp.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    detectionworker.h \
    gstreamerrtsp.h \
    mainwindow.h

FORMS += \
    mainwindow.ui

win32 {
    message("Win32 enabled")
    PATH_LIB = $$system(echo %USERPROFILE%\\Desktop)
    RC_ICONS = $$PWD/icons/objectdedect.ico
    INCLUDEPATH += $$PATH_LIB\lib\opencv\include
    LIBS += -L$$PATH_LIB\lib\opencv\x64\vc16\lib -lopencv_world4100

    INCLUDEPATH += \
            $$PATH_LIB\lib\gstreamer\include \
            $$PATH_LIB\lib\gstreamer\include\gstreamer-1.0 \
            $$PATH_LIB\lib\gstreamer\include\glib-2.0 \
            $$PATH_LIB\lib\gstreamer\lib\glib-2.0\include

    LIBS += -L$$PATH_LIB\lib\gstreamer\lib \
       -lgstreamer-1.0 \
       -lgobject-2.0 \
       -lglib-2.0 \
       -lgstapp-1.0 \
       -lgstvideo-1.0 \
       -lgstbase-1.0 \
       -lgstrtsp-1.0 \
       -lgstrtp-1.0 \
       -lgstnet-1.0
}

unix:!macx:!ios:!android {
    message("linux enabled")
    #sudo apt install libopencv-dev
    INCLUDEPATH += /usr/lib
    INCLUDEPATH += /usr/local/lib
    INCLUDEPATH += /usr/local/include/opencv4
    INCLUDEPATH += /usr/include/opencv4
    LIBS += -lopencv_core -lopencv_dnn -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -lopencv_objdetect -lopencv_video -lopencv_videoio
}

macx {
    message("macx enabled")
    # Info.plist dosyası
    QMAKE_INFO_PLIST = $$PWD/Info.plist
    # OpenCV yolları
    INCLUDEPATH += /opt/homebrew/Cellar/opencv/4.11.0/include/opencv4
    # OpenCV kütüphaneleri
    LIBS += -L/opt/homebrew/Cellar/opencv/4.11.0/lib \
            -lopencv_core.4.11.0 \
            -lopencv_highgui.4.11.0 \
            -lopencv_imgproc.4.11.0 \
            -lopencv_imgcodecs.4.11.0 \
            -lopencv_videoio.4.11.0 \
            -lopencv_dnn.4.11.0 \
            -lopencv_tracking.4.11.0 \
            -lopencv_video.4.11.0 \
            -lopencv_objdetect.4.11.0
    # Kamera izinleri için gerekli framework'ler
    LIBS += -framework AVFoundation
    LIBS += -framework CoreMedia
    LIBS += -framework CoreVideo
    # Kamera izinleri için ek ayarlar
    QMAKE_MAC_SDK = macosx
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.15
    # Entitlements ekle
    QMAKE_ASSET_CATALOGS += $$PWD/Assets.xcassets
    QMAKE_ENTITLEMENTS += $$PWD/Entitlements.plist
}

ios {
    message("ios enabled")
    QMAKE_INFO_PLIST = ./ios/Info.plist
    QMAKE_ASSET_CATALOGS = $$PWD/ios/Assets.xcassets
    QMAKE_ASSET_CATALOGS_APP_ICON = "AppIcon"
}

android {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
    # Your existing OpenCV configurations
    INCLUDEPATH += $$PWD/OpenCV-android-sdk/sdk/native/jni/include
    DEPENDPATH += $$PWD/OpenCV-android-sdk/sdk/native/jni/include
    contains(ANDROID_TARGET_ARCH,arm64-v8a) {
        LIBS += -L$$PWD/OpenCV-android-sdk/sdk/native/libs/arm64-v8a -lopencv_java4
        LIBS += \
            -L$$PWD/OpenCV-android-sdk/sdk/native/staticlibs/arm64-v8a \
            -lopencv_ml\
            -lopencv_objdetect\
            -lopencv_photo\
            -lopencv_video\
            -lopencv_calib3d\
            -lopencv_features2d\
            -lopencv_highgui\
            -lopencv_flann\
            -lopencv_videoio\
            -lopencv_imgcodecs\
            -lopencv_imgproc\
            -lopencv_core
        ANDROID_EXTRA_LIBS = $$PWD/OpenCV-android-sdk/sdk/native/libs/arm64-v8a/libopencv_java4.so
    }
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources.qrc

DISTFILES += \   
    models/coco.names.1 \
    models/haarcascade_frontalface_default.xml \
    models/yolov4-tiny.cfg \
    models/yolov4-tiny.weights
