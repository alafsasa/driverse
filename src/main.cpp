#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <QStatusBar>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Driverse");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("driverse");

    QMainWindow window;
    window.setWindowTitle("Driverse - Linux Storage Cleaner");
    window.resize(1024, 700);

    QWidget *centralWidget = new QWidget(&window);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    
    QLabel *label = new QLabel("Driverse - Phase 0 Foundation Complete");
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("font-size: 18px; padding: 20px;");
    
    layout->addWidget(label);
    centralWidget->setLayout(layout);
    
    window.setCentralWidget(centralWidget);
    window.statusBar()->showMessage("Ready - Build system initialized");

    window.show();
    return app.exec();
}
