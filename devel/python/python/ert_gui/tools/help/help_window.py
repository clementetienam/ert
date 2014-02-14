from PyQt4.QtCore import Qt, QUrl, pyqtSignal
from PyQt4.QtGui import QLabel, QVBoxLayout, QColor, QDesktopServices, QDialog, QMainWindow, QWidget

from ert_gui.tools import HelpCenter


class HelpWindow(QMainWindow):
    help_prefix = None
    default_help_string = "No help available!"
    validation_template = ("<html>"
                           "<table style='background-color: #ffefef;'width='100%%'>"
                           "<tr><td style='font-weight: bold; padding-left: 5px;'>Notice:</td></tr>"
                           "<tr><td style='padding: 5px;'>%s</td></tr>"
                           "</table>"
                           "</html>")

    visibilityChanged = pyqtSignal(bool)

    def __init__(self, help_center_name, parent=None):
        QMainWindow.__init__(self, parent, Qt.WindowStaysOnTopHint)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), QColor(255, 255, 224))
        self.setPalette(palette)
        self.setAutoFillBackground(True)
        self.setMinimumWidth(300)
        self.setMinimumHeight(250)
        self.setWindowTitle("Help")

        central_widget = QWidget()

        layout = QVBoxLayout()
        central_widget.setLayout(layout)

        self.link_widget = QLabel()
        self.link_widget.setStyleSheet("font-weight: bold")
        self.link_widget.setMinimumHeight(20)

        self.help_widget = QLabel(HelpWindow.default_help_string)
        self.help_widget.setWordWrap(True)
        self.help_widget.setTextFormat(Qt.RichText)
        self.help_widget.linkActivated.connect(self.openHelpURL)

        layout.addWidget(self.link_widget)
        layout.addWidget(self.help_widget)
        layout.addStretch(1)

        HelpCenter.getHelpCenter(help_center_name).addListener(self)

        self.__geometry = None
        self.setCentralWidget(central_widget)


    def openHelpURL(self, q_string):
        url = QUrl(q_string)
        QDesktopServices.openUrl(url)


    def setHelpMessage(self, help_link, message):
        self.link_widget.setText(help_link)
        self.help_widget.setText(message)

    def setVisible(self, visible):
        QMainWindow.setVisible(self, visible)

        if visible and self.__geometry is not None:
            self.setGeometry(self.__geometry)
        else:
            self.__geometry = self.geometry()

        self.visibilityChanged.emit(visible)


    def keyPressEvent(self, event):
        if event.key() != Qt.Key_Escape:
            QMainWindow.keyPressEvent(self, event)

    def closeEvent(self, event):
        self.setVisible(False)

