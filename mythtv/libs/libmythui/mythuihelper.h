#ifndef MYTHUIHELPERS_H_
#define MYTHUIHELPERS_H_

#include <QStringList>
#include <QString>
#include <QFont>
#include <QMutex>
#include <QThreadPool>

#include "mythexp.h"
#include "themeinfo.h"

#define DEFAULT_UI_THEME "Terra"
#define FALLBACK_UI_THEME "MythCenter-wide"

class MythUIHelperPrivate;
class MythPainter;
class MythImage;
class QImage;
class QWidget;
class Settings;
class QPixmap;
class QSize;

typedef enum ImageCacheMode
{
    kCacheNormal          = 0x0,
    kCacheIgnoreDisk      = 0x1,
    kCacheCheckMemoryOnly = 0x2,
    kCacheForceStat       = 0x4,
} ImageCacheMode;

struct MPUBLIC MythUIMenuCallbacks
{
    void (*exec_program)(const QString &cmd);
    void (*exec_program_tv)(const QString &cmd);
    void (*configplugin)(const QString &cmd);
    void (*plugin)(const QString &cmd);
    void (*eject)(void);
};

class MPUBLIC MythUIHelper
{
  public:
    void Init(MythUIMenuCallbacks &cbs);

    MythUIMenuCallbacks *GetMenuCBs(void);

    void LoadQtConfig(void);
    void UpdateImageCache(void);

    MythImage *GetImageFromCache(const QString &url);
    MythImage *CacheImage(const QString &url, MythImage *im,
                          bool nodisk = false);
    void RemoveFromCacheByURL(const QString &url);
    void RemoveFromCacheByFile(const QString &fname);
    bool IsImageInCache(const QString &url);
    QString GetThemeCacheDir(void);

    void IncludeInCacheSize(MythImage *im);
    void ExcludeFromCacheSize(MythImage *im);

    Settings *qtconfig(void);

    bool IsScreenSetup(void);
    bool IsTopScreenInitialized(void);

    // which the user may have set to be different from the raw screen size
    void GetScreenSettings(float &wmult, float &hmult);
    void GetScreenSettings(int &width, float &wmult,
                           int &height, float &hmult);
    void GetScreenSettings(int &xbase, int &width, float &wmult,
                           int &ybase, int &height, float &hmult);

    // This returns the raw (drawable) screen size
    void GetScreenBounds(int &xbase, int &ybase, int &width, int &height);

    // Parse an X11 style command line (-geometry) string
    static void ParseGeometryOverride(const QString &geometry);
    bool IsGeometryOverridden(void);

    QPixmap *LoadScalePixmap(QString filename, bool fromcache = true);
    QImage *LoadScaleImage(QString filename, bool fromcache = true);
    MythImage *LoadCacheImage(QString srcfile, QString label,
                              MythPainter *painter,
                              ImageCacheMode cacheMode = kCacheNormal);

    void ThemeWidget(QWidget *widget);

    QString FindThemeDir(const QString &themename);
    QString FindMenuThemeDir(const QString &menuname);
    QString GetThemeDir(void);
    QString GetThemeName(void);
    QStringList GetThemeSearchPath(void);
    QString GetMenuThemeDir(void);
    QList<ThemeInfo> GetThemes(ThemeType type);

    bool FindThemeFile(QString &filename);

    QFont GetBigFont(void);
    QFont GetMediumFont(void);
    QFont GetSmallFont(void);

    // event wrappers
    void DisableScreensaver(void);
    void RestoreScreensaver(void);
    // Reset screensaver idle time, for input events that X doesn't see
    // (e.g., lirc)
    void ResetScreensaver(void);

    // actually do it
    void DoDisableScreensaver(void);
    void DoRestoreScreensaver(void);
    void DoResetScreensaver(void);

    // get the current status
    bool GetScreensaverEnabled(void);
    bool GetScreenIsAsleep(void);

    static void SetX11Display(const QString &display);
    static QString GetX11Display(void);

    static QString x11_display;

    static MythUIHelper *getMythUI(void);
    static void destroyMythUI(void);

    void AddCurrentLocation(QString location);
    QString RemoveCurrentLocation(void);
    QString GetCurrentLocation(bool fullPath = false, bool mainStackOnly = true);

    QThreadPool *GetImageThreadPool(void);

    double GetPixelAspectRatio(void) const;
    QSize GetBaseSize(void) const;

    void SetFontStretch(int stretch);
    int GetFontStretch(void) const;

  protected:
    MythUIHelper();
   ~MythUIHelper();

  private:
    void SetPalette(QWidget *widget);
    void InitializeScreenSettings(void);

    void ClearOldImageCache(void);
    void RemoveCacheDir(const QString &dirname);

    MythUIHelperPrivate *d;

    QMutex m_locationLock;
    QStringList m_currentLocation;
};

MPUBLIC MythUIHelper *GetMythUI();
MPUBLIC void DestroyMythUI();

#endif

