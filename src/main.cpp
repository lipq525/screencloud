//
// ScreenCloud - An easy to use screenshot sharing application
// Copyright (C) 2016 Olav Sortland Thoresen <olav.s.th@gmail.com>
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU General Public License for more details.
//

#include <Python.h>
#include <QtSingleApplication>
#include <QSettings>
#include <QMessageBox>
#include <utils/log.h>
#include <QDir>
#include <QImage>
#include <QStandardPaths>
#include <utils/OS.h>
#include <utils/delay.h>
#include <plugin/pluginmanager.h>
#include <screenshooter.h>
#include <uploadmanager.h>
#include <systemtrayicon.h>
#include <firstrunwizard/firstrunwizard.h>
#include <PythonQt.h>

#ifdef PYTHONQT_QTALL_FOUND
#include <PythonQt_QtAll.h>
#else
#include <PythonQt_QtBindings.h>
#endif

int main(int argc, char *argv[])
{
        QtSingleApplication a(argc, argv);
        int retcode;

        a.setOrganizationName("screencloud");
        a.setApplicationName("ScreenCloud");
        a.setQuitOnLastWindowClosed(false);
        //Create data location for storing plugins if it dosen't exist
        QDir d;
        d.mkpath(PluginManager::pluginPath());
        //Prepare settings
        QSettings settings("screencloud", "ScreenCloud");
        settings.setValue("config-version", "1.1");
        bool firstRun = settings.value("first-run", true).toBool();
        //Parse command line switches
        QStringList cmdline_args = a.arguments();
        if(cmdline_args.contains("--help") || cmdline_args.contains("-h") )
        {
            qDebug() << "USAGE: screencloud [-p] -c [-f|-w|-a|-fl] -s [service]\n\n"
                        "  -p, --preferences        Start with preferences window open\n"
                        "  -c, --cli                Enable CLI mode\n"
                        "  -f, --fullscreen         Capture full screen\n"
                        "  -w, --window <id>        Capture window with <id>\n"
                        "  -a, --area <x,y,w,h>     Capture an area of the screen\n"
                        "  -fl, --file <filename>   Upload <filename> instead of capturing screen\n"
                        "  -s, --service <name>     Service to upload to. Defaults to 'clipboard'\n\n"
                        "  -v, --version            Print version number to the console\n"
                        "  -h, --help               Show this help screen\n";
            return 0;
        }
        if(cmdline_args.contains("--version") || cmdline_args.contains("-v") )
        {
            INFO(QObject::tr("ScreenCloud version ") + QString(VERSION) + " (" + QString(OPERATING_SYSTEM) + ")");
            return 0;
        }
        //Setup the python interpreter
#ifdef PYTHONQT_IGNORE_SITE
        PythonQt::init(PythonQt::RedirectStdOut | PythonQt::IgnoreSiteModule);
#else
        PythonQt::init(PythonQt::RedirectStdOut);
#endif
#ifdef PYTHONQT_QTALL_FOUND
        PythonQt_QtAll::init();
#else
        PythonQt_init_QtBindings();
#endif

        PythonQt::self()->getMainModule().evalScript("import sys"); //Required for addSysPath on some systems
#if PYTHONQT_QTALL_FOUND
        /*
         * We assume that hadError() will work if PythonQt is new enough to use Qt_All
         */
        if(PythonQt::self()->hadError())
        {
            WARNING(QObject::tr("Failed to import sys module. Check your Python installation."));
            QMessageBox::critical(NULL, QObject::tr("Failed to import sys"), QObject::tr("Failed to import sys module. Check your Python installation."));
        }
#endif

        PythonQt::self()->addSysPath(a.applicationDirPath() + QDir::separator() + "modules");
#ifdef Q_OS_WIN
        PythonQt::self()->addSysPath(a.applicationDirPath() + QDir::separator() + "modules"  + QDir::separator() + "python-stdlib-native");
		PythonQt::self()->addSysPath(a.applicationDirPath() + QDir::separator() + "modules"  + QDir::separator() + "Crypto.zip");
#elif defined Q_OS_MACX
        PythonQt::self()->addSysPath(a.applicationDirPath() + QDir::separator() + "../Resources/modules");
        PythonQt::self()->addSysPath(a.applicationDirPath() + QDir::separator() + "../Resources/modules"  + QDir::separator() + "python-stdlib-native");
#else
        QStringList dataLocations = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        QString path;
        Q_FOREACH(path, dataLocations)
        {
            PythonQt::self()->addSysPath(path + QDir::separator() + "screencloud" + QDir::separator() + "modules");
        }
#endif

        if(cmdline_args.contains("--cli") || cmdline_args.contains("-c"))
        {
            //Running in CLI mode, do not show GUI
            if(firstRun)
            {
                CRITICAL(QObject::tr("This is the first time you're running ScreenCloud. Refusing to run in CLI mode."));
                return 1;
            }
            ScreenShooter screenShooter;
            QImage screenshot;
            if(cmdline_args.contains("--area") || cmdline_args.contains("-a"))
            {
                int areaGeomIndex = cmdline_args.indexOf("--area");
                if(areaGeomIndex == -1)
                {
                    areaGeomIndex = cmdline_args.indexOf("-a");
                }
                areaGeomIndex += 1;
                if(areaGeomIndex > 0 && areaGeomIndex < cmdline_args.count())
                {
                    QString areaString = cmdline_args.at(areaGeomIndex);
                    INFO(QObject::tr("Grabbing area (") + areaString + ")");
                    QRect area(QPoint(areaString.split(",")[0].toUInt(), areaString.split(",")[1].toUInt()), QSize(areaString.split(",")[2].toUInt(), areaString.split(",")[3].toUInt()));
                    screenshot = screenShooter.captureSelection(area);
                }else
                {
                    CRITICAL(QObject::tr("No area provided. Format: --area x,y,width,height. Example: --area 0,0,640,480"));
                    return 1;
                }
            }
            else if(cmdline_args.contains("--window") || cmdline_args.contains("-w"))
            {
                //Get the window id
                int winIdIndex = cmdline_args.indexOf("--window");
                if(winIdIndex == -1)
                {
                    winIdIndex = cmdline_args.indexOf("-w");
                }
                winIdIndex += 1;
                if(winIdIndex > 0 && winIdIndex < cmdline_args.count())
                {
                    settings.beginGroup("main");
                    bool captureWindowBorders = settings.value("capture-window-borders", false).toBool();
                    settings.endGroup();
                    WId windowId = (WId)cmdline_args.at(winIdIndex).toInt();
                    INFO(QObject::tr("Grabbing window with id ") + QString::number((int)windowId));
                    screenshot = screenShooter.captureWindow(windowId), captureWindowBorders;
                }else
                {
                    CRITICAL(QObject::tr("No window id provided."));
                    return 1;
                }
            }else if(cmdline_args.contains("--file") || cmdline_args.contains("-fl"))
            {
                int filePathIndex = cmdline_args.indexOf("--file");
                if(filePathIndex == -1)
                {
                    filePathIndex = cmdline_args.indexOf("-fl");
                }
                filePathIndex += 1;
                if(filePathIndex > 0 && filePathIndex < cmdline_args.count())
                {
                    QString filePath = cmdline_args.at(filePathIndex);
                    if(!screenshot.load(filePath))
                    {
                        CRITICAL(QObject::tr("Failed to read file ") + filePath);
                        return 1;
                    }
                    INFO(QObject::tr("Using file ") + filePath);
                }else
                {
                    CRITICAL(QObject::tr("No file path set."));
                    return 1;
                }
            }
            else
            {
                //Default to fullscreen
                if(!cmdline_args.contains("--fullscreen") && !cmdline_args.contains("-f"))
                {
                    INFO(QObject::tr("No --area or --window set. Defaulting to fullscreen."));
                }else
                {
                    INFO(QObject::tr("Capturing fullscreen."))
                }
                settings.beginGroup("main");
                bool captureMultipleMonitors = settings.value("capture-multiple-monitors", false).toBool();
                settings.endGroup();
                screenshot = screenShooter.captureFullscreen(captureMultipleMonitors);
            }
            UploadManager up;
            QString serviceShortname = up.getDefaultService();
            if(cmdline_args.contains("--service") || cmdline_args.contains("-s"))
            {
                int serviceNameIndex = cmdline_args.indexOf("--service");
                if(serviceNameIndex == -1)
                {
                    serviceNameIndex = cmdline_args.indexOf("-s");
                }
                serviceNameIndex += 1;
                serviceShortname = cmdline_args.at(serviceNameIndex);
            }
            up.upload(screenshot, serviceShortname, up.getUploader(serviceShortname)->getFilename(), true);
            retcode = 0;
        }else
        {
            if(a.isRunning())
            {
                INFO("Running instance detected.")
                if(cmdline_args.contains("--fullscreen") && !cmdline_args.contains("-f"))
                {
                    INFO("Sending message 'fullscreen' to running instance.");
                    a.sendMessage("fullscreen");
                }
                else if(cmdline_args.contains("--area") || cmdline_args.contains("-a"))
                {
                    INFO("Sending message 'area' to running instance.");
                    a.sendMessage("area");
                }
                else if(cmdline_args.contains("--window") || cmdline_args.contains("-w"))
                {
                    INFO("Sending message 'window' to running instance.");
                    a.sendMessage("window");
                }
                else
                {
                    QMessageBox::critical(NULL, QObject::tr("Running instance detected"), QObject::tr("ScreenCloud is already running. Please close the running instance before starting a new one."));
                    return 1;
                }
                return 0;
            }
            //Show GUI
            if(firstRun)
            {
                FirstRunWizard wizard;
                if(wizard.exec() != QWizard::Accepted)
                {
                    exit(0);
                }
                settings.setValue("first-run", false);
            }
            QString iconColor;
            QStringList iconColorSwitches = cmdline_args.filter("--icon-color=");
            if(iconColorSwitches.size() > 0)
            {
                iconColor = iconColorSwitches[0].split("=")[1];
            }

            //Check if there's a tray available
            int tries = 0;
            while(!QSystemTrayIcon::isSystemTrayAvailable()) {
                tries++;
                INFO("Checking for systray, try: " + QString::number(tries));
                delay(1000);
                if(tries > 3)
                {
                    QMessageBox::critical(NULL, "System Tray not available", "ScreenCloud requires a system tray to function properly. Installing 'sni-qt' or 'indicator-application' might fix the problem.");
                    break;
                }
            }
            //Check if we should start with preferences window open
            bool openPerfWindow = cmdline_args.contains("--preferences") || cmdline_args.contains("-p");

            //Show the trayicon
            SystemTrayIcon w(NULL, iconColor, openPerfWindow);
            QObject::connect(&a, SIGNAL(messageReceived(QString)), &w, SLOT(messageReceivedFromOtherProcess(QString)));
            w.show();

            retcode = a.exec();
        }
        //Clean up
        PythonQt::cleanup();
        //Exit
        return retcode;
}
