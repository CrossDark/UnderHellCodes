#pragma once

#include <QMainWindow>
#include <QFutureWatcher>

class QSpinBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;

/* 地图生成 GUI:worldgen 核心直接内嵌编译,可调整参数并预览地图 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void postLog(const QString &msg);   /* 供工作线程回调(线程安全) */

private slots:
    void generate();
    void browseOutput();
    void onGenerationFinished();
    void loadImage(const QString &path);
    void cycleTheme();                  /* 自动 / 浅色 / 深色 循环切换 */
    void onSystemColorSchemeChanged();  /* 系统外观变化时(仅自动模式生效) */

private:
    void appendLog(const QString &msg);
    void applyTheme();                  /* 按 m_themeMode 应用 QSS 并更新按钮文案 */
    bool isSystemDark() const;          /* 读取系统当前外观 */

    QSpinBox      *m_seed;
    QSpinBox      *m_faults;
    QSpinBox      *m_water;
    QSpinBox      *m_dispersion;
    QSpinBox      *m_width;
    QSpinBox      *m_height;
    QSpinBox      *m_lineWidth;
    QSpinBox      *m_slices;
    QCheckBox     *m_grid;
    QCheckBox     *m_fill;
    QLineEdit     *m_output;
    QLabel        *m_mapLabel;
    QPlainTextEdit *m_log;
    QPushButton   *m_themeBtn;
    QFutureWatcher<int> *m_watcher;
    QString        m_pendingImage;
    int            m_themeMode = 0;     /* 0=自动, 1=浅色, 2=深色 */
};
