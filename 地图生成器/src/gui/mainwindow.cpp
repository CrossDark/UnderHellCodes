#include "mainwindow.h"
#include "worldgen_core.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStyleHints>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <QImageReader>
#include <QPixmap>

/* 核心日志回调:把 worldgen_core 的消息队列投递到主线程显示 */
static MainWindow *g_win = nullptr;

static void coreLog(const char *msg)
{
    if (g_win) {
        const QString s = QString::fromUtf8(msg);
        QMetaObject::invokeMethod(g_win, [s]() {
            g_win->postLog(s.trimmed());
        }, Qt::QueuedConnection);
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("地狱之下 - 地图生成器"));
    setWindowIcon(QIcon(QStringLiteral(":/images/world-generator-icon.png")));

    /* ---------- 顶部标题栏 ---------- */
    auto *headerIcon = new QLabel(this);
    headerIcon->setPixmap(QIcon(QStringLiteral(":/images/world-generator-icon.png")).pixmap(40, 40));
    headerIcon->setFixedSize(44, 44);
    auto *headerTitle = new QLabel(QStringLiteral("地狱之下 · 地图生成器"), this);
    headerTitle->setObjectName(QStringLiteral("headerTitle"));
    auto *headerSub = new QLabel(QStringLiteral("调整参数并预览生成的世界地图"), this);
    headerSub->setObjectName(QStringLiteral("headerSubtitle"));
    auto *headerText = new QVBoxLayout;
    headerText->setSpacing(1);
    headerText->setContentsMargins(0, 0, 0, 0);
    headerText->addWidget(headerTitle);
    headerText->addWidget(headerSub);
    auto *header = new QHBoxLayout;
    header->setSpacing(12);
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(headerIcon, 0, Qt::AlignVCenter);
    header->addLayout(headerText);
    header->addStretch(1);

    /* 主题切换按钮(右上角):自动 / 浅色 / 深色 三态循环 */
    m_themeBtn = new QPushButton(QStringLiteral("外观:自动"), this);
    m_themeBtn->setObjectName(QStringLiteral("themeBtn"));
    m_themeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_themeBtn, &QPushButton::clicked, this, &MainWindow::cycleTheme);
    header->addWidget(m_themeBtn, 0, Qt::AlignVCenter);

    /* ---------- 左侧:参数面板 ---------- */
    auto *paramBox = new QGroupBox(QStringLiteral("参数"), this);
    auto *form = new QFormLayout(paramBox);
    form->setSpacing(10);
    form->setContentsMargins(2, 8, 2, 2);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    m_seed = new QSpinBox(paramBox);
    m_seed->setRange(0, 2000000000);
    m_seed->setValue(7);
    m_seed->setSpecialValueText(QStringLiteral("随机(0)"));

    /* 种子旁的随机按钮:点击生成随机种子 */
    auto *seedRandomBtn = new QPushButton(QStringLiteral("🎲"), paramBox);
    seedRandomBtn->setObjectName(QStringLiteral("seedRandomBtn"));
    seedRandomBtn->setCursor(Qt::PointingHandCursor);
    seedRandomBtn->setToolTip(QStringLiteral("随机生成种子"));
    seedRandomBtn->setFixedSize(32, m_seed->sizeHint().height());
    connect(seedRandomBtn, &QPushButton::clicked, this, [this]() {
        m_seed->setValue(QRandomGenerator::global()->bounded(1, 2000000001));
    });
    auto *seedRow = new QHBoxLayout;
    seedRow->setSpacing(6);
    seedRow->setContentsMargins(0, 0, 0, 0);
    seedRow->addWidget(m_seed, 1);
    seedRow->addWidget(seedRandomBtn);

    m_faults = new QSpinBox(paramBox);
    m_faults->setRange(0, 5000);
    m_faults->setValue(0);
    m_faults->setSpecialValueText(QStringLiteral("自动(0)"));

    m_water = new QSpinBox(paramBox);
    m_water->setRange(0, 100);
    m_water->setValue(65);
    m_water->setSuffix(QStringLiteral(" %"));

    m_dispersion = new QSpinBox(paramBox);
    m_dispersion->setRange(0, 100);
    m_dispersion->setValue(0);
    m_dispersion->setSuffix(QStringLiteral(" %"));
    m_dispersion->setToolTip(QStringLiteral("0=大片大陆, 100=分散群岛"));

    m_width = new QSpinBox(paramBox);
    m_width->setRange(128, 8192);
    m_width->setSingleStep(256);
    m_width->setValue(2560);

    m_height = new QSpinBox(paramBox);
    m_height->setRange(64, 4096);
    m_height->setSingleStep(128);
    m_height->setValue(1440);

    m_lineWidth = new QSpinBox(paramBox);
    m_lineWidth->setRange(1, 20);
    m_lineWidth->setValue(3);

    m_slices = new QSpinBox(paramBox);
    m_slices->setRange(0, 99);
    m_slices->setValue(0);
    m_slices->setSpecialValueText(QStringLiteral("关闭(0)"));

    m_grid = new QCheckBox(QStringLiteral("经纬网格"), paramBox);
    m_fill = new QCheckBox(QStringLiteral("分层设色"), paramBox);

    m_output = new QLineEdit(paramBox);
    m_output->setText(QStringLiteral("图片/地图_生成.png"));
    auto *browseBtn = new QPushButton(QStringLiteral("浏览…"), paramBox);
    browseBtn->setCursor(Qt::PointingHandCursor);
    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::browseOutput);
    auto *outRow = new QHBoxLayout;
    outRow->setSpacing(6);
    outRow->setContentsMargins(0, 0, 0, 0);
    outRow->addWidget(m_output, 1);
    outRow->addWidget(browseBtn);

    form->addRow(QStringLiteral("种子"), seedRow);
    form->addRow(QStringLiteral("故障次数"), m_faults);
    form->addRow(QStringLiteral("水占比"), m_water);
    form->addRow(QStringLiteral("离散度"), m_dispersion);
    form->addRow(QStringLiteral("宽度"), m_width);
    form->addRow(QStringLiteral("高度"), m_height);
    form->addRow(QStringLiteral("线宽"), m_lineWidth);
    form->addRow(QStringLiteral("等高线切片"), m_slices);
    form->addRow(m_grid);
    form->addRow(m_fill);
    form->addRow(QStringLiteral("输出路径"), outRow);

    auto *genBtn = new QPushButton(QStringLiteral("生成地图"), paramBox);
    genBtn->setObjectName(QStringLiteral("primaryBtn"));
    genBtn->setCursor(Qt::PointingHandCursor);
    genBtn->setMinimumHeight(42);
    connect(genBtn, &QPushButton::clicked, this, &MainWindow::generate);

    auto *left = new QVBoxLayout;
    left->setSpacing(12);
    left->setContentsMargins(0, 0, 0, 0);
    left->addWidget(paramBox);
    left->addWidget(genBtn);
    left->addStretch(1);

    /* ---------- 右侧:地图预览(白色卡片) ---------- */
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("previewScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    m_mapLabel = new QLabel(scroll);
    m_mapLabel->setObjectName(QStringLiteral("mapLabel"));
    m_mapLabel->setAlignment(Qt::AlignCenter);
    m_mapLabel->setText(QStringLiteral("尚未生成"));
    m_mapLabel->setMinimumSize(400, 300);
    scroll->setWidget(m_mapLabel);

    /* ---------- 底部:日志(深色控制台) ---------- */
    m_log = new QPlainTextEdit(this);
    m_log->setObjectName(QStringLiteral("logView"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setMinimumHeight(110);
    m_log->setMaximumHeight(180);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setSpacing(14);
    root->setContentsMargins(20, 18, 20, 16);

    auto *mainLayout = new QHBoxLayout;
    mainLayout->setSpacing(14);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addLayout(left, 0);
    mainLayout->addWidget(scroll, 1);

    root->addLayout(header);
    root->addLayout(mainLayout, 1);
    root->addWidget(m_log, 0);

    setCentralWidget(central);
    resize(1180, 800);

    /* ---------- 异步生成(worldgen 核心内嵌,后台线程执行) ---------- */
    m_watcher = new QFutureWatcher<int>(this);
    connect(m_watcher, &QFutureWatcher<int>::finished,
            this, &MainWindow::onGenerationFinished);

    g_win = this;
    worldgen_set_log(coreLog);

    /* 读取上次主题模式(0=自动, 1=浅色, 2=深色);默认自动 */
    QSettings settings(QStringLiteral("com.crossdark.worldgen"), QStringLiteral("GUI"));
    m_themeMode = settings.value(QStringLiteral("themeMode"), 0).toInt();
    if (m_themeMode < 0 || m_themeMode > 2)
        m_themeMode = 0;
    applyTheme();

    /* 监听系统外观实时变化(仅"自动"模式下生效,需 Qt >= 6.5) */
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &MainWindow::onSystemColorSchemeChanged);
#endif
}

MainWindow::~MainWindow()
{
    if (g_win == this)
        g_win = nullptr;
}

void MainWindow::postLog(const QString &msg)
{
    appendLog(msg);
}

void MainWindow::appendLog(const QString &msg)
{
    m_log->appendPlainText(msg);
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

void MainWindow::generate()
{
    if (m_watcher->isRunning()) {
        appendLog(QStringLiteral("已在生成中,请稍候…"));
        return;
    }

    QString outPath = m_output->text().trimmed();
    if (outPath.isEmpty()) {
        appendLog(QStringLiteral("请先填写输出路径。"));
        return;
    }

    /* 相对路径 -> 相对当前工作目录转绝对;目录不存在则创建,
     * 不可写时(如从 Finder 启动 .app 时 cwd 为 /)回退到主目录 */
    if (!QDir::isAbsolutePath(outPath))
        outPath = QDir::current().absoluteFilePath(outPath);
    const QString parentPath = QFileInfo(outPath).absolutePath();
    if (!QDir(parentPath).exists() && !QDir().mkpath(parentPath)) {
        const QString alt = QDir::home().absoluteFilePath(QFileInfo(outPath).fileName());
        appendLog(QStringLiteral("无法创建目录 %1,改用 %2").arg(parentPath, alt));
        outPath = alt;
        if (!QDir().mkpath(QFileInfo(outPath).absolutePath())) {
            appendLog(QStringLiteral("仍然无法创建目录,已取消生成。"));
            return;
        }
    }
    m_output->setText(outPath);

    const int seed      = m_seed->value();
    const int faults    = m_faults->value();
    const int water     = m_water->value();
    const int disp      = m_dispersion->value();
    const int w         = m_width->value();
    const int h         = m_height->value();
    const int lw        = m_lineWidth->value();
    const int grid      = m_grid->isChecked() ? 1 : 0;
    const int slices    = m_slices->value();
    const int fill      = m_fill->isChecked() ? 1 : 0;

    QStringList tags;
    if (grid) tags << QStringLiteral("[经纬网格]");
    if (slices > 0) tags << QStringLiteral("切片%1").arg(slices);
    if (fill) tags << QStringLiteral("[分层设色]");
    if (disp > 0) tags << QStringLiteral("离散%1%").arg(disp);
    appendLog(QStringLiteral("开始生成: 种子=%1 故障=%2 水=%3% 离散=%4% %5x%6 线宽=%7 -> %8 %9")
                  .arg(seed)
                  .arg(faults)
                  .arg(water)
                  .arg(disp)
                  .arg(w)
                  .arg(h)
                  .arg(lw)
                  .arg(outPath, tags.join(QLatin1Char(' '))));

    m_pendingImage = outPath;
    /* UTF-8 路径必须在此线程内转成字节并随 lambda 存活 */
    QByteArray out8 = outPath.toUtf8();

    auto future = QtConcurrent::run([seed, faults, water, disp, w, h, lw,
                                     grid, slices, fill, out8]() {
        return worldgen_run(seed, faults, water, disp, w, h, lw,
                            grid, slices, fill, out8.constData());
    });
    m_watcher->setFuture(future);
}

void MainWindow::browseOutput()
{
    const QString dir = QDir::currentPath();
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存地图"), dir,
        QStringLiteral("PNG 图片 (*.png);;所有文件 (*)"));
    if (!file.isEmpty())
        m_output->setText(file);
}

void MainWindow::onGenerationFinished()
{
    const int code = m_watcher->result();
    appendLog(QStringLiteral("生成结束,退出码 %1").arg(code));
    if (code == 0 && !m_pendingImage.isEmpty())
        loadImage(m_pendingImage);
}

void MainWindow::loadImage(const QString &path)
{
    QImage img(path);
    if (img.isNull()) {
        appendLog(QStringLiteral("无法加载图片: %1").arg(path));
        return;
    }
    /* 按预览区等比例缩放(不拉伸) */
    const int viewW = m_mapLabel->width();
    const int viewH = m_mapLabel->height();
    QImage scaled = img.scaled(viewW, viewH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_mapLabel->setPixmap(QPixmap::fromImage(scaled));
    m_mapLabel->setText(QString());
    appendLog(QStringLiteral("已加载: %1 (%2x%3)").arg(path).arg(img.width()).arg(img.height()));
}

/* 读取系统当前外观是否为深色 */
bool MainWindow::isSystemDark() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    /* Qt < 6.5 回退:通过调色板文字/窗口亮度比判断 */
    const auto pal = palette();
    return pal.color(QPalette::WindowText).lightness() >
           pal.color(QPalette::Window).lightness();
#endif
}

/* 自动 / 浅色 / 深色 循环切换,并持久化模式 */
void MainWindow::cycleTheme()
{
    m_themeMode = (m_themeMode + 1) % 3;   /* 0->1->2->0 */
    applyTheme();
    QSettings settings(QStringLiteral("com.crossdark.worldgen"), QStringLiteral("GUI"));
    settings.setValue(QStringLiteral("themeMode"), m_themeMode);
}

/* 系统外观变化回调:仅在"自动"模式下实时跟随 */
void MainWindow::onSystemColorSchemeChanged()
{
    if (m_themeMode == 0)
        applyTheme();
}

/* 按 m_themeMode 决定实际深浅并应用 QSS,同步更新按钮文案 */
void MainWindow::applyTheme()
{
    bool dark;
    QString label;
    switch (m_themeMode) {
    case 1:  dark = false;               label = QStringLiteral("外观:浅色"); break;
    case 2:  dark = true;                label = QStringLiteral("外观:深色"); break;
    default: dark = isSystemDark();      label = QStringLiteral("外观:自动"); break;
    }

    const QString res = dark ? QStringLiteral(":/style-dark.qss")
                             : QStringLiteral(":/style.qss");
    QFile f(res);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        qApp->setStyleSheet(QString::fromUtf8(f.readAll()));

    m_themeBtn->setText(label);
}
