#include "BrowserApplication.h"
#include "AdBlockManager.h"
#include "BookmarkManager.h"
#include "CookieJar.h"
#include "CookieWidget.h"
#include "DatabaseFactory.h"
#include "DownloadManager.h"
#include "FaviconStorage.h"
#include "HistoryManager.h"
#include "HistoryWidget.h"
#include "MainWindow.h"
#include "SearchEngineManager.h"
#include "Settings.h"
#include "NetworkAccessManager.h"
#include "RequestInterceptor.h"
#include "URLSuggestionModel.h"
#include "UserAgentManager.h"
#include "UserScriptManager.h"
#include "ViperSchemeHandler.h"
#include "WebPage.h"

#include <vector>
#include <QDir>
#include <QUrl>
#include <QDebug>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>

BrowserApplication::BrowserApplication(int &argc, char **argv) :
    QApplication(argc, argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("Vaccarelli"));
    QCoreApplication::setApplicationName(QStringLiteral("Viper Browser"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.5"));

    setAttribute(Qt::AA_DontShowIconsInMenus, false);

    // Setup request interceptor and attach to the default web profile
    m_requestInterceptor = new RequestInterceptor;
    auto webProfile = QWebEngineProfile::defaultProfile();
    webProfile->setRequestInterceptor(m_requestInterceptor);

    // Setup cookie manager
    m_cookieUI = new CookieWidget();
    webProfile->cookieStore()->loadAllCookies();

    // Instantiate viper scheme handler and add to the web engine profile
    m_viperSchemeHandler = new ViperSchemeHandler(this);
    webProfile->installUrlSchemeHandler("viper", m_viperSchemeHandler);

    // Setup the private browsing profile
    m_privateProfile = new QWebEngineProfile(this);
    m_privateProfile->setRequestInterceptor(m_requestInterceptor);
    m_privateProfile->installUrlSchemeHandler("viper", m_viperSchemeHandler);

    // Load settings
    m_settings = std::make_shared<Settings>();

    // Initialize favicon storage module
    m_faviconStorage = DatabaseFactory::createWorker<FaviconStorage>(m_settings->getPathValue(QStringLiteral("FaviconPath")));

    // Initialize bookmarks manager
    m_bookmarks = DatabaseFactory::createWorker<BookmarkManager>(m_settings->getPathValue(QStringLiteral("BookmarkPath")));

    // Initialize cookie jar
    m_cookieJar = DatabaseFactory::createWorker<CookieJar>(m_settings->getPathValue(QStringLiteral("CookiePath")));

    // Initialize download manager
    m_downloadMgr = new DownloadManager;
    m_downloadMgr->setDownloadDir(m_settings->getValue(QStringLiteral("DownloadDir")).toString());
    connect(webProfile, &QWebEngineProfile::downloadRequested, m_downloadMgr, &DownloadManager::onDownloadRequest);

    // Instantiate the history manager
    m_historyMgr = DatabaseFactory::createWorker<HistoryManager>(m_settings->getPathValue(QStringLiteral("HistoryPath")));

    m_historyWidget = nullptr;

    // Create url suggestion model
    m_suggestionModel = new URLSuggestionModel;

    // Create network access managers
    m_networkAccessMgr = new NetworkAccessManager;
    m_networkAccessMgr->setCookieJar(m_cookieJar.get());

    m_downloadMgr->setNetworkAccessManager(m_networkAccessMgr);

    m_privateNetworkAccessMgr = new NetworkAccessManager;
    CookieJar *privateJar = new CookieJar(QString("%1.fake").arg(m_settings->getPathValue(QStringLiteral("CookiePath"))), QStringLiteral("FakeCookies"), true);
    m_privateNetworkAccessMgr->setCookieJar(privateJar);

    // Setup user agent manager before settings
    m_userAgentMgr = new UserAgentManager(m_settings);

    // Setup user script manager
    m_userScriptMgr = new UserScriptManager(m_settings);

    // Apply web settings
    m_settings->applyWebSettings();

    // Load search engine information
    SearchEngineManager::instance().loadSearchEngines(m_settings->getPathValue(QStringLiteral("SearchEnginesFile")));

    // Load ad block subscriptions (will do nothing if disabled)
    AdBlockManager::instance().loadSubscriptions();

    // Set browser's saved sessions file
    m_sessionMgr.setSessionFile(m_settings->getPathValue(QStringLiteral("SessionFile")));

    // Connect aboutToQuit signal to browser's session management slot
    connect(this, &BrowserApplication::aboutToQuit, this, &BrowserApplication::beforeBrowserQuit);

    // TODO: check if argument has been given (argc > 1) and load the resource into a new window
}

BrowserApplication::~BrowserApplication()
{
    for (int i = 0; i < m_browserWindows.size(); ++i)
    {
        QPointer<MainWindow> m = m_browserWindows.at(i);
        if (!m.isNull())
            delete m.data();
    }

    // Prevents network access manager from attempting to delete cookie jar and causing segfault
    m_cookieJar->setParent(nullptr);

    delete m_downloadMgr;
    delete m_suggestionModel;
    delete m_networkAccessMgr;
    delete m_privateNetworkAccessMgr;
    delete m_historyWidget;
    delete m_userAgentMgr;
    delete m_userScriptMgr;
    delete m_privateProfile;
    delete m_requestInterceptor;
    delete m_viperSchemeHandler;
    delete m_cookieUI;
}

BrowserApplication *BrowserApplication::instance()
{
    return static_cast<BrowserApplication*>(QCoreApplication::instance());
}

BookmarkManager *BrowserApplication::getBookmarkManager()
{
    return m_bookmarks.get();
}

CookieJar *BrowserApplication::getCookieJar()
{
    return m_cookieJar.get();
}

std::shared_ptr<Settings> BrowserApplication::getSettings()
{
    return m_settings;
}

DownloadManager *BrowserApplication::getDownloadManager()
{
    return m_downloadMgr;
}

FaviconStorage *BrowserApplication::getFaviconStorage()
{
    return m_faviconStorage.get();
}

HistoryManager *BrowserApplication::getHistoryManager()
{
    return m_historyMgr.get();
}

HistoryWidget *BrowserApplication::getHistoryWidget()
{
    if (!m_historyWidget)
    {
        m_historyWidget = new HistoryWidget;
        m_historyWidget->setHistoryManager(m_historyMgr.get());
    }

    return m_historyWidget;
}

NetworkAccessManager *BrowserApplication::getNetworkAccessManager()
{
    return m_networkAccessMgr;
}

NetworkAccessManager *BrowserApplication::getPrivateNetworkAccessManager()
{
    return m_privateNetworkAccessMgr;
}

QWebEngineProfile *BrowserApplication::getPrivateBrowsingProfile()
{
    return m_privateProfile;
}

URLSuggestionModel *BrowserApplication::getURLSuggestionModel()
{
    return m_suggestionModel;
}

UserAgentManager *BrowserApplication::getUserAgentManager()
{
    return m_userAgentMgr;
}

UserScriptManager *BrowserApplication::getUserScriptManager()
{
    return m_userScriptMgr;
}

CookieWidget *BrowserApplication::getCookieManager()
{
    m_cookieUI->resetUI();
    return m_cookieUI;
}

MainWindow *BrowserApplication::getNewWindow()
{
    bool firstWindow = m_browserWindows.empty();

    MainWindow *w = new MainWindow(m_settings, m_bookmarks.get(), false);
    m_browserWindows.append(w);
    connect(w, &MainWindow::aboutToClose, this, &BrowserApplication::maybeSaveSession);

    w->show();

    // Check if this is the first window since the application has started - if so, handle
    // the startup mode behavior depending on the user's configuration setting
    if (firstWindow)
    {
        StartupMode mode = m_settings->getValue(QStringLiteral("StartupMode")).value<StartupMode>();
        switch (mode)
        {
            case StartupMode::LoadHomePage:
                w->loadUrl(QUrl::fromUserInput(m_settings->getValue(QStringLiteral("HomePage")).toString()));
                break;
            case StartupMode::LoadBlankPage:
                w->loadBlankPage();
                break;
            case StartupMode::RestoreSession:
                m_sessionMgr.restoreSession(w);
                break;
        }

        AdBlockManager::instance().updateSubscriptions();
    }
    else
    {
        // Treat new window as a new tab, and check if new tab behavior is set to
        // open a blank page or load a home page URL
        if (m_settings->getValue("NewTabsLoadHomePage").toBool())
            w->loadUrl(QUrl::fromUserInput(m_settings->getValue(QStringLiteral("HomePage")).toString()));
        else
            w->loadBlankPage();
    }

    return w;
}

MainWindow *BrowserApplication::getNewPrivateWindow()
{
    MainWindow *w = new MainWindow(m_settings, m_bookmarks.get(), true);
    m_browserWindows.append(w);

    w->show();
    return w;
}

void BrowserApplication::clearHistory(HistoryType histType, QDateTime start)
{
    // Check if browsing history flag is set
    if ((histType & HistoryType::Browsing) == HistoryType::Browsing)
    {
        if (start.isNull())
            m_historyMgr->clearAllHistory();
        else
            m_historyMgr->clearHistoryFrom(start);

        emit resetHistoryMenu();
    }

    // Check if cookie flag is set
    if ((histType & HistoryType::Cookies) == HistoryType::Cookies)
    {
        if (start.isNull())
            m_cookieJar->eraseAllCookies();
        else
            m_cookieJar->clearCookiesFrom(start);
    }

    //todo: support clearing form and search data

    // Reload URLs in the suggestion model
    m_suggestionModel->loadURLs();
}

void BrowserApplication::clearHistoryRange(HistoryType histType, std::pair<QDateTime, QDateTime> range)
{
    // Check for a valid start-end date-time pair
    if (!range.first.isValid() || !range.second.isValid())
        return;

    // Check if browsing history flag is set
    if ((histType & HistoryType::Browsing) == HistoryType::Browsing)
    {
        m_historyMgr->clearHistoryInRange(range);
        emit resetHistoryMenu();
    }

    // Check if cookie flag is set
    if ((histType & HistoryType::Cookies) == HistoryType::Cookies)
        m_cookieJar->clearCookiesInRange(range);

    //todo: support clearing form and search data

    // Reload URLs in the suggestion model
    m_suggestionModel->loadURLs();
}

void BrowserApplication::beforeBrowserQuit()
{
    StartupMode mode = m_settings->getValue(QStringLiteral("StartupMode")).value<StartupMode>();
    if (mode != StartupMode::RestoreSession && m_sessionMgr.alreadySaved())
        return;

    // Get all windows that can be saved
    std::vector<MainWindow*> windows;
    for (int i = 0; i < m_browserWindows.size(); ++i)
    {
        QPointer<MainWindow> m = m_browserWindows.at(i);
        if (!m.isNull() && !m->isPrivate())
        {
            windows.push_back(m.data());
        }
    }

    if (!windows.empty())
        m_sessionMgr.saveState(windows);
}

void BrowserApplication::maybeSaveSession()
{
    // Note: don't need to check if startup mode is set to restore session, this slot will not be
    //       activated unless that condition is already met
    std::vector<MainWindow*> windows;
    for (int i = 0; i < m_browserWindows.size(); ++i)
    {
        QPointer<MainWindow> m = m_browserWindows.at(i);
        if (!m.isNull() && !m->isPrivate())
        {
            windows.push_back(m.data());
        }
    }

    // Only save session in this method if there's one window left. Saving more than one window is
    // handled by beforeBrowserQuit() method
    if (windows.empty() || windows.size() > 1)
        return;

    m_sessionMgr.saveState(windows);
}
