// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QObject>
#include <QWidget>
#include <QMainWindow>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QProgressBar>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include <QMetaObject>
#include <QCryptographicHash>
#include <QInputDialog>
#include <QFile>
#include <QDebug>
#include <QTime>
#include <QDate> 
#include <QDateTime> 
#include <QDir>
#include <QMap>
#include <QHeaderView>
#include <QBuffer>
#include <QFileInfo>
#include <QTimer>
#include <QElapsedTimer>
#include <QProcess>
#include <QDialog>
#include <QDesktopServices>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslConfiguration>
#include <QTcpSocket>
#include <QTcpServer>
#include <QUdpSocket> 
#include <QHostAddress>
#include <QNetworkInterface>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTabWidget>
#include <QSplitter>
#include <QScrollArea>
#include <ftdi.h>
#include <vector>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <atomic>
#include <functional>
#include <QEventLoop>

#include "Core.h"

#if defined(__APPLE__) || defined(Q_OS_MAC)
#include <libusb.h>
#elif defined(Q_OS_WIN)
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h> 
#endif

// --- КОНСТАНТЫ ---
const QString T_ADDR = "192.168.90.100"; 
const QString G_ADDR = "192.168.90.102"; 
const quint16 T_S_P = 8081;
const quint16 T_U_P = 28496;
const quint16 G_U_P = 3500;

const uint32_t E_B = 0x710000;
const uint32_t E_L = 0xEF800;
const uint32_t P_SZ = 256;
const uint32_t S_4K = 4096;
const uint32_t B_64 = 65536;

// --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ---
QString p_s(uint64_t n) {
    if (n % (1 << 20) == 0) return QString::number(n / (1 << 20)) + " MB";
    if (n % (1 << 10) == 0) return QString::number(n / (1 << 10)) + " KB";
    return QString::number(n) + " B";
}

QString s_256(const QByteArray &d) {
    return QString(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()).toUpper();
}

QString f_h_d(const std::vector<uint8_t>& d, uint32_t sa) {
    QString o; const int r = 16;
    for (size_t i = 0; i < d.size(); i += r) {
        o += QString("%1  ").arg(sa + i, 8, 16, QChar('0')).toUpper();
        QString a;
        for (int j = 0; j < r; j++) {
            if (i + j < d.size()) {
                uint8_t b = d[i+j];
                o += QString("%1 ").arg(b, 2, 16, QChar('0')).toUpper();
                if (b >= 32 && b < 127) a += (char)b; else a += ".";
            } else { o += "   "; }
        }
        o += " |" + a + "|\n";
    }
    return o;
}

QString getRootPath() {
    QByteArray appImageEnv = qgetenv("APPIMAGE");
    if (!appImageEnv.isEmpty()) {
        QFileInfo fi(appImageEnv);
        return fi.absolutePath();
    }
    QString path = QCoreApplication::applicationDirPath();
#if defined(Q_OS_MACOS)
    QDir d(path);
    d.cdUp(); d.cdUp(); d.cdUp();
    return d.absolutePath();
#else
    return path;
#endif
}

// --- NATIVE HTTP SERVER (ИСПРАВЛЕННЫЙ И УЛУЧШЕННЫЙ) ---

class FileSender : public QObject {
    Q_OBJECT
public:
    // Добавлен флаг isHeadRequest
    FileSender(QTcpSocket* s, QString path, bool isHeadRequest, QObject* p=nullptr) 
        : QObject(p), sock(s), filePath(path), isHead(isHeadRequest) {
        sock->setParent(this);
        connect(sock, &QTcpSocket::bytesWritten, this, &FileSender::sendNextChunk);
        connect(sock, &QTcpSocket::disconnected, this, &FileSender::deleteLater);
    }

    void start() {
        file.setFileName(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            QString head = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            sock->write(head.toUtf8());
            sock->flush();
            sock->disconnectFromHost();
            return;
        }

        totalSize = file.size();
        startTime = QDateTime::currentMSecsSinceEpoch();
        
        // Определение MIME типов
        QString mime = "application/octet-stream";
        if (filePath.endsWith(".json")) mime = "application/json";
        else if (filePath.endsWith(".html")) mime = "text/html";
        else if (filePath.endsWith(".png")) mime = "image/png";
        else if (filePath.endsWith(".txt")) mime = "text/plain";

        // ВАЖНО: Добавлены заголовки Accept-Ranges и Content-Transfer-Encoding
        // Это позволяет машине качать файл корректно (как wget/curl)
        QString head = QString("HTTP/1.1 200 OK\r\n"
                               "Server: TigardInternal/1.0\r\n"
                               "Content-Type: %1\r\n"
                               "Content-Length: %2\r\n"
                               "Accept-Ranges: bytes\r\n" 
                               "Content-Transfer-Encoding: binary\r\n"
                               "Connection: close\r\n"
                               "Access-Control-Allow-Origin: *\r\n" 
                               "\r\n").arg(mime).arg(totalSize);
        
        sock->write(head.toUtf8());

        // Если это HEAD запрос, отправляем только заголовки и закрываем
        if (isHead) {
            sock->flush();
            sock->disconnectFromHost();
            return;
        }

        // Если GET, начинаем слать данные
        sendNextChunk(0);
    }

private slots:
    void sendNextChunk(qint64) {
        if (isHead) return; 

        // Ограничиваем буфер сокета, чтобы не забить память, если клиент медленный
        if (sock->bytesToWrite() > 128 * 1024) {
            return; // Ждем, пока освободится буфер
        }

        if (file.atEnd()) {
            if (sock->state() == QAbstractSocket::ConnectedState) {
                sock->disconnectFromHost();
            }
            return;
        }

        // Читаем по 64KB
        QByteArray data = file.read(65536);
        if (!data.isEmpty()) {
            sock->write(data);
            bytesSent += data.size();

            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastUpdate > 200) { 
                double elapsed = (now - startTime) / 1000.0;
                double speed = (elapsed > 0) ? (bytesSent / elapsed) : 0;
                double pct = (totalSize > 0) ? (double)bytesSent / totalSize * 100.0 : 0;
                int eta = (speed > 0) ? (totalSize - bytesSent) / speed : 0;
                emit progress(pct, bytesSent, totalSize, speed, eta);
                lastUpdate = now;
            }
        }
    }

signals:
    void progress(float pct, qint64 sent, qint64 total, double speed, int eta);

private:
    QTcpSocket* sock;
    QFile file;
    QString filePath;
    bool isHead;
    qint64 totalSize = 0;
    qint64 bytesSent = 0;
    qint64 startTime = 0;
    qint64 lastUpdate = 0;
};

class NativeHttpServer : public QTcpServer {
    Q_OBJECT
public:
    NativeHttpServer(QObject *parent = nullptr) : QTcpServer(parent) {}
    void setRoot(QString path) { rootPath = path; }

signals:
    void logMsg(QString msg);
    void uploadProgress(float pct, double mbSent, double mbTotal, double mbSpeed, int eta);

protected:
    void incomingConnection(qintptr socketDescriptor) override {
        QTcpSocket *socket = new QTcpSocket(this); 
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            delete socket;
            return;
        }

        connect(socket, &QTcpSocket::readyRead, [this, socket](){
            if (!socket->canReadLine()) return;

            QString line = QString(socket->readLine()).trimmed();
            QStringList parts = line.split(' ');
            
            if (parts.size() < 2) return;

            QString method = parts[0].toUpper();
            QString url = parts[1];

            // Теперь обрабатываем и GET и HEAD
            if (method == "GET" || method == "HEAD") {
                if (url.startsWith("/")) url.remove(0, 1);
                url = QByteArray::fromPercentEncoding(url.toUtf8());
                
                // Убираем параметры запроса (все что после ?), если есть
                int qMark = url.indexOf("?");
                if (qMark != -1) url = url.left(qMark);

                QString cleanPath = QDir(rootPath).filePath(url);
                QFileInfo fi(cleanPath);

                // Защита от выхода за пределы папки (Path Traversal)
                if (!fi.absoluteFilePath().startsWith(QDir(rootPath).absolutePath())) {
                    socket->close(); 
                    return; 
                }

                emit logMsg(QString("[HTTP] %1 %2 (%3)").arg(method).arg(url).arg(socket->peerAddress().toString()));

                if (fi.exists() && fi.isFile()) {
                    // Передаем флаг isHead
                    FileSender *sender = new FileSender(socket, cleanPath, (method == "HEAD"), this);
                    connect(sender, &FileSender::progress, this, [this](float p, qint64 s, qint64 t, double spd, int eta){
                        emit uploadProgress(p, s/1048576.0, t/1048576.0, spd/1048576.0, eta);
                    });
                    sender->start();
                    
                    // Отключаем этот лямбда-слот, чтобы не читать следующие заголовки как новые запросы
                    socket->disconnect(this); 
                } else {
                    QString head = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    socket->write(head.toUtf8());
                    socket->disconnectFromHost();
                }
            }
        });
    }
private:
    QString rootPath;
};

// --- IO_Dr (DRIVER) ---
class IO_Dr {
public:
    struct Cfg { int i; int f; bool fr; };
    struct ftdi_context *ft; Cfg c;
    IO_Dr() : ft(nullptr) {}
    ~IO_Dr() { cl(); }
    void cl() { if(ft) { ftdi_usb_close(ft); ftdi_free(ft); ft = nullptr; } }
    bool op(const Cfg &cg, QString &e) {
        qDebug() << "[DRV] Opening Tigard Interface" << cg.i << "Freq:" << cg.f;
        if (ft) cl(); ft = ftdi_new(); if (!ft) { e = "drv err"; return false; }
        ftdi_set_interface(ft, (cg.i == 2) ? INTERFACE_B : INTERFACE_A);
        int r = ftdi_usb_open_desc(ft, 0x0403, 0x6010, nullptr, nullptr); 
        if (r < 0) { 
            e = QString("Connection Failed: %1").arg(ftdi_get_error_string(ft)); 
            qDebug() << "[DRV]" << e;
            return false; 
        }
        qDebug() << "[DRV] Connected successfully";
        if (ftdi_usb_reset(ft) < 0) return false; if (ftdi_set_bitmode(ft, 0x00, BITMODE_MPSSE) < 0) return false;
        ftdi_set_latency_timer(ft, 1);
        int sf = cg.f <= 0 ? 1000000 : cg.f; int d = (30000000 / sf) - 1; if(d < 0) d=0;
        std::vector<uint8_t> s = {0x8A, 0x97, 0x8D, 0x86, (uint8_t)(d&0xFF), (uint8_t)((d>>8)&0xFF), 0x80, 0x08, 0x0B};
        ftdi_write_data(ft, s.data(), s.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); this->c = cg;
        tr(new uint8_t[1]{0xAB},1); std::this_thread::sleep_for(std::chrono::milliseconds(10));
        tr(new uint8_t[1]{0xB7},1); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return true;
    }
    std::vector<uint8_t> xch(const std::vector<uint8_t> &tx, int rl = 0) {
        if (!ft) return {};
        std::vector<uint8_t> b = {0x80, 0x00, 0x0B};
        if (!tx.empty()) { b.push_back(0x11); int l = tx.size()-1; b.push_back(l&0xFF); b.push_back((l>>8)&0xFF); b.insert(b.end(), tx.begin(), tx.end()); }
        if (rl > 0) { b.push_back(0x20); int l = rl-1; b.push_back(l&0xFF); b.push_back((l>>8)&0xFF); }
        b.insert(b.end(), {0x80, 0x08, 0x0B});
        ftdi_write_data(ft, b.data(), b.size());
        std::vector<uint8_t> rs;
        if (rl>0) { rs.resize(rl); int o=0; while(o<rl) { int rc = ftdi_read_data(ft, rs.data()+o, rl-o); if(rc>0) o+=rc; else std::this_thread::sleep_for(std::chrono::microseconds(100)); } }
        return rs;
    }
    void tr(const uint8_t* d, size_t l) { xch(std::vector<uint8_t>(d, d+l), 0); }
    void wr() { tr(new uint8_t[1]{0x06}, 1); }
    uint8_t rs() { return xch({0x05}, 1)[0]; }
    bool bw(double t) { int s = t/0.001; for(int i=0;i<s;i++) { if((rs()&1)==0) return true; std::this_thread::sleep_for(std::chrono::microseconds(100)); } return false; }
    std::vector<uint8_t> rj() { return xch({0x9F}, 3); }
    std::vector<uint8_t> rd(uint32_t a, uint32_t l, std::function<void(int,int)> cb=nullptr) {
        std::vector<uint8_t> r; r.reserve(l);
        for(uint32_t d=0; d<l;) {
            uint32_t ch = std::min((uint32_t)65536, l-d); uint32_t ca = a+d;
            std::vector<uint8_t> cm;
            if(c.fr) cm={0x0C, (uint8_t)(ca>>24), (uint8_t)(ca>>16), (uint8_t)(ca>>8), (uint8_t)ca, 0x00};
            else cm={0x13, (uint8_t)(ca>>24), (uint8_t)(ca>>16), (uint8_t)(ca>>8), (uint8_t)ca};
            auto p = xch(cm, ch); r.insert(r.end(), p.begin(), p.end()); d+=ch; if(cb) cb(d, l);
        }
        return r;
    }
    void ser(uint32_t s, uint32_t l, std::function<void(int,int)> cb) {
        auto es = [&](uint32_t sb, uint32_t rl, uint32_t rh) {
            auto og = rd(sb, S_4K); auto nb = og; uint32_t lo = std::max(sb, rl) - sb; uint32_t hi = std::min(sb + S_4K, rh) - sb;
            if (lo < hi) {
                for (size_t i = lo; i < hi; i++) nb[i] = 0xFF;
                if (nb != og) { wr(); xch({0x20, (uint8_t)((sb>>24)&0xFF), (uint8_t)((sb>>16)&0xFF), (uint8_t)((sb>>8)&0xFF), (uint8_t)(sb&0xFF)}, 0); bw(5.0); pb(sb, nb, nullptr); }
            }
        };
        uint32_t e = s + l; uint32_t f = s & ~(S_4K - 1); uint32_t ls = (e - 1) & ~(S_4K - 1);
        if (f == ls) { es(f, s, e); if(cb) cb(l, l); return; }
        es(f, s, e); uint32_t c = f + S_4K;
        while (c < ls) {
             if ((c + B_64 <= ls) && (c % B_64 == 0)) { wr(); xch({0xDC, (uint8_t)((c>>24)&0xFF), (uint8_t)((c>>16)&0xFF), (uint8_t)((c>>8)&0xFF), (uint8_t)(c&0xFF)}, 0); bw(12.0); c += B_64; } 
             else { wr(); xch({0x20, (uint8_t)((c>>24)&0xFF), (uint8_t)((c>>16)&0xFF), (uint8_t)((c>>8)&0xFF), (uint8_t)(c&0xFF)}, 0); bw(5.0); c += S_4K; }
             if (cb) cb(std::min(c - s, l), l);
        }
        es(ls, s, e); if(cb) cb(l, l);
    }
    void ce() { wr(); tr(new uint8_t[1]{0xC7}, 1); if (!bw(900.0)) throw std::runtime_error("CE T/O"); }
    void pb(uint32_t b, const std::vector<uint8_t> &bf, std::function<void(int,int)> cb) {
        for(size_t o=0; o<bf.size();) {
            size_t ch = std::min((size_t)(256-(b+o)%256), bf.size()-o); wr();
            std::vector<uint8_t> cm = {0x12, (uint8_t)((b+o)>>24), (uint8_t)((b+o)>>16), (uint8_t)((b+o)>>8), (uint8_t)((b+o))};
            cm.insert(cm.end(), bf.begin()+o, bf.begin()+o+ch); xch(cm, 0); bw(0.1); o+=ch; if(cb) cb(o, bf.size());
        }
    }
};

// --- GUI MAIN ---
class GuiMain : public QMainWindow {
    Q_OBJECT
public:
    GuiMain() {
        dr = new IO_Dr(); nm = new QNetworkAccessManager(this); us = new QUdpSocket(this);
        httpServer = new NativeHttpServer(this);

        us->bind(QHostAddress(QHostAddress::AnyIPv4), 0);
        
        httpRootPath = getRootPath(); 
        
        connect(httpServer, &NativeHttpServer::logMsg, this, &GuiMain::al);
        connect(httpServer, &NativeHttpServer::uploadProgress, this, [this](float pct, double sent, double tot, double spd, int eta){
             hp->setValue((int)pct);
             lhp->setText(QString("Upload: %1% | %2/%3 MB | %4 MB/s | ETA: %5:%6")
                          .arg(pct, 0, 'f', 1).arg(sent, 0, 'f', 1).arg(tot, 0, 'f', 1)
                          .arg(spd, 0, 'f', 2).arg(eta/60,2,10,QChar('0')).arg(eta%60,2,10,QChar('0')));
        });

        ui(); 
        initProjects(); 
        rf(); 
        
        refreshIpList();

        connect(nm, &QNetworkAccessManager::finished, this, &GuiMain::nf);
       
        pt = new QTimer(this); connect(pt, &QTimer::timeout, this, &GuiMain::ptk); pt->start(2000); 
        ut = new QTimer(this); connect(ut, &QTimer::timeout, this, &GuiMain::sup);
        QString tid = SysCore::g_t_s();
        lblTigardId->setText(tid);
    }
    ~GuiMain() { 
        if(pt) pt->stop(); if(ut) ut->stop();
        if(httpServer->isListening()) httpServer->close();
        delete dr; 
    }

signals: void ls(QString m); void ps(int v, int x);
private slots:
    void al(QString m) { tl->append(m); tl->moveCursor(QTextCursor::End); }
    void up(int v, int x) { if (x > 0) { if (pb->maximum() != x) pb->setMaximum(x); pb->setValue(v); lp->setText(QString::number((float)v/x*100.0, 'f', 1)+"%"); } else { pb->setValue(0); lp->setText("0%"); } }
    
    void sup() { QByteArray d = QByteArray::fromHex("18BABBA0AD"); us->writeDatagram(d, QHostAddress(G_ADDR), G_U_P); al(QString("[UDP] Sent Packet to %1:%2").arg(G_ADDR).arg(G_U_P)); }
    
    void ouc() { 
        if (ut->isActive()) { 
            ut->stop(); 
            bu->setText("Cycle Unlock"); 
            bu->setStyleSheet(""); 
            al("[UDP] Cycle Stopped"); 
        } else { 
            ut->start(30000); 
            sup(); 
            bu->setText("Stop Cycle"); 
            bu->setStyleSheet("background-color: #2e7d32; color:white; font-weight: bold; border: 1px solid #4CAF50;"); 
            al("[UDP] Cycle Started (30s)"); 
        } 
    }

    void ptk() { ph(G_ADDR, lg); ph(T_ADDR, lc); }
    void ph(const QString &i, QLabel *l) { 
        QProcess *p = new QProcess(this); 
        QStringList a;
#ifdef Q_OS_WIN
        a << "-n" << "1" << "-w" << "1000" << i;
#else
        a << "-c" << "1" << "-W" << "1" << i; 
#endif
        connect(p, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, p, l](int e, QProcess::ExitStatus){ 
            if (!l) return;
            if (e == 0) l->setStyleSheet("QLabel { background:#00c853; border:1px solid #000; border-radius:8px; }"); 
            else l->setStyleSheet("QLabel { background:#d32f2f; border:1px solid #000; border-radius:8px; }"); 
            p->deleteLater(); 
        }); 
        p->start("ping", a); 
    }
    void oc() { IO_Dr::Cfg c; c.i = ci->currentIndex() + 1; c.f = cf->currentData().toInt(); c.fr = ck->isChecked(); QString e; if (dr->op(c, e)) { al(QString("[i] Connected. Freq: %1 Hz").arg(c.f)); cn = true; } else { al(QString("[ERR] %1").arg(e)); QMessageBox::critical(this, "Connect Error", e); } }
    void oj() { rt([this]() { try { auto i = dr->rj(); QString s = "[i] JEDEC ID: "; for(auto b : i) s += QString("%1 ").arg(b, 2, 16, QChar('0')).toUpper(); emit ls(s); } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); } }); }
    
    void ofr() { QString p = getProjFile("full_dump.bin"); uint32_t s = ghi("Flash Size (bytes)", 1024*1024*32); if(s==0) return; rt([this, p, s]() { try { emit ls(QString("[i] Reading Full %1 -> %2").arg(p_s(s)).arg(p)); auto d = dr->rd(0, s, [this](int k, int t){ emit ps(k, t); }); QFile f(p); if (!f.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot open output file"); if (f.write((const char*)d.data(), d.size()) != static_cast<qint64>(d.size())) throw std::runtime_error("Incomplete file write"); emit ls("[i] Saved. SHA256: " + s_256(QByteArray((char*)d.data(), d.size()))); } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); } }); }
    void ofw() { QString p = QFileDialog::getOpenFileName(this, "Select Raw Dump"); if (p.isEmpty()) return; rt([this, p]() { try { QFile f(p); if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open input file"); QByteArray d = f.readAll(); emit ls(QString("[i] Writing Full (%1)...").arg(p_s(d.size()))); emit ls("[i] Chip Erase..."); dr->ce(); emit ls("[i] Programming..."); std::vector<uint8_t> b(d.begin(), d.end()); dr->pb(0, b, [this](int k, int t){ emit ps(k, t); }); emit ls("✅ Write Done."); } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); } }); }
    void oarl() { uint32_t a = ghi("Address", 0); uint32_t l = ghi("Length", 256); if(l>4096) l=4096; rt([this, a, l](){ try { auto d = dr->rd(a, l); emit ls(f_h_d(d, a)); } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); } }); }
    void oarf() { uint32_t a = ghi("Address", 0); uint32_t l = ghi("Length", 4096); QString p = QFileDialog::getSaveFileName(this, "Save Chunk"); if(p.isEmpty()) return; rt([this, a, l, p](){ try { auto d = dr->rd(a, l, [this](int k, int t){ emit ps(k,t); }); QFile f(p); if (!f.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot open output file"); if (f.write((const char*)d.data(), d.size()) != static_cast<qint64>(d.size())) throw std::runtime_error("Incomplete file write"); emit ls("✅ Chunk Saved."); } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); } }); }
    void oaw() { uint32_t a = ghi("Address", 0); QString p = QFileDialog::getOpenFileName(this, "Select File"); if(p.isEmpty()) return; rt([this, a, p](){ try { QFile f(p); if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open input file"); QByteArray d = f.readAll(); std::vector<uint8_t> b(d.begin(), d.end()); emit ls("[i] Erasing Range..."); dr->ser(a, d.size(), [this](int k,int t){emit ps(k,t);}); emit ls("[i] Programming..."); dr->pb(a, b, [this](int k,int t){emit ps(k,t);}); emit ls("✅ Write Done."); } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); } }); }
    void oac() { uint32_t a = ghi("Address", 0); QString p = QFileDialog::getOpenFileName(this, "Select File to Compare"); if(p.isEmpty()) return; rt([this, a, p](){ try { QFile f(p); if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open comparison file"); QByteArray x = f.readAll(); auto g = dr->rd(a, x.size(), [this](int k, int t){ emit ps(k,t);}); if(memcmp(x.data(), g.data(), x.size())==0) emit ls("✅ MATCH"); else emit ls("❌ MISMATCH"); } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); } }); }
    
    bool v_j() {
        auto j = dr->rj();
        if (j.size() < 3 || j[0] != 0x20 || j[1] != 0xBB || j[2] != 0x19) {
            emit ls("❌ Connection Error! Invalid JEDEC ID. Check wiring.");
            return false;
        }
        return true;
    }

    void ore() {
        QString td = getActiveProjectDir();
        if(td.isEmpty()) {
            bool ok; QString name = QInputDialog::getText(this, "Create Project", "Enter Base Project Name:", QLineEdit::Normal, "MX_black", &ok);
            if (!ok || name.isEmpty()) return;
            QDir d(getRootPath() + "/projects"); if(!d.exists()) d.mkdir("."); d.mkdir(name);
            refreshProjectList(name); td = getActiveProjectDir();
        }
        emit ls("Saving to: " + QFileInfo(td).fileName());
        rt([this, td]() { try { 
            if (!v_j()) return;
            emit ls("[i] Smart Read Enc (3 matches, max 10 attempts)..."); 
            QMap<QString, QVector<QByteArray>> goodReads; 
            QVector<QByteArray> damagedReads;
            bool ok = false; 
            
            for (int at = 1; at <= 10; ++at) {
                emit ls(QString("--- Attempt #%1 ---").arg(at)); 
                auto rd = dr->rd(E_B, E_L, [this](int k, int t){ emit ps(k, t); }); 
                QByteArray rb((char*)rd.data(), rd.size()); 
                
                bool magicOk = (rb.size() >= 4 && (unsigned char)rb[0] == 0x41 && (unsigned char)rb[1] == 0x50 && (unsigned char)rb[2] == 0x4F && (unsigned char)rb[3] == 0x42);
                QString h = s_256(rb); 
                
                if (magicOk && rb.size() == E_L) {
                    goodReads[h].append(rb);
                    emit ls(QString("  SHA: %1... Match: %2/3").arg(h.left(8)).arg(goodReads[h].size())); 
                    if (goodReads[h].size() >= 3) { 
                        ok = true; 
                        for(int n=1; n<=3; n++) {
                            QString fn = td + QString("/enc_%1_valid.bin").arg(n);
                            QFile f(fn); if (f.open(QIODevice::WriteOnly)) { f.write(rb); f.close(); }
                        }
                        emit ls("✅ SUCCESS! 3 identical reads."); 
                        emit ls("\n!!! Unplug HDMI from Tigard and let MCU start !!!\n"); 
                        break;
                    }
                } else {
                    emit ls("❌ Invalid Data (header or Size).");
                    damagedReads.append(rb);
                }
                if (at < 10) std::this_thread::sleep_for(std::chrono::milliseconds(300));
            } 
            
            int dIdx = 1;
            // Якщо не вдалося знайти 3 однакові, зберігаємо всі хороші як пошкоджені
            if (!ok) {
                emit ls("❌ FAILED: Maximum attempts (10) reached.");
                for (auto it = goodReads.begin(); it != goodReads.end(); ++it) {
                    for (const auto& data : it.value()) {
                        QString dfn = td + QString("/enc_%1_damaged.bin").arg(dIdx++);
                        QFile df(dfn); if (df.open(QIODevice::WriteOnly)) { df.write(data); df.close(); }
                    }
                }
            }
            // Зберігаємо явно пошкоджені (без правильного header)
            for (const auto& data : damagedReads) {
                QString dfn = td + QString("/enc_%1_damaged.bin").arg(dIdx++);
                QFile df(dfn); if (df.open(QIODevice::WriteOnly)) { df.write(data); df.close(); }
            }
            
            QMetaObject::invokeMethod(this, "refreshEncFileList", Qt::QueuedConnection);
        } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); } });
    }

    void oee() { if(QMessageBox::No == QMessageBox::question(this, "Confirm Erase", "Are you sure you want to erase ENC Data?", QMessageBox::Yes|QMessageBox::No)) return; rt([this]() { try { 
        if (!v_j()) return;
        if (!verifyEncFiles()) { emit ls("❌ Erase blocked: Need 3 valid ENC files with correct size and headers."); return; }
        emit ls("[i] Erasing ENC..."); dr->ser(E_B, E_L, [this](int k, int t){ emit ps(k, t); }); emit ls("✅ Erase Done."); 
    } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); } }); }
    
    void och() {
        QString p = QFileDialog::getOpenFileName(this, "Select File to Check");
        if (p.isEmpty()) return;
        QFile f(p);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray raw = f.readAll();
            f.close();
            bool m = (raw.size() >= 4 && (unsigned char)raw[0] == 0x41 && (unsigned char)raw[1] == 0x50 && (unsigned char)raw[2] == 0x4F && (unsigned char)raw[3] == 0x42);
            bool s = (raw.size() == E_L);
            if (m && s) al("✅ File is VALID (Size OK, header OK)");
            else al(QString("❌ File is INVALID: %1 %2").arg(s ? "" : "Size ERR").arg(m ? "" : "header ERR"));
        }
    }
    
    void ope() { 
        QString p;
        if (sender() == btnWriteSelected) {
            QString fn = cmbEncFiles->currentText();
            if (fn.isEmpty()) return;
            p = getActiveProjectDir() + "/" + fn;
        } else {
            p = QFileDialog::getOpenFileName(this, "Select File");
        }
        if (p.isEmpty()) return; 
        rt([this, p]() { try { 
            if (!v_j()) return;
            QFile f(p); if(!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Open err"); 
            QByteArray d = f.readAll(); f.close();
            if(d.size()>E_L) { emit ls("❌ Too big for ENC"); return; } 
            std::vector<uint8_t> b(d.begin(), d.end()); 
            emit ls("[i] Erasing ENC..."); dr->ser(E_B, E_L, [this](int k, int t){ emit ps(k, t); }); 
            emit ls("[i] Writing ENC data..."); dr->pb(E_B, b, [this](int k, int t){ emit ps(k, t); });
            emit ls("[i] Verifying..."); auto r = dr->rd(E_B, d.size(), [this](int k, int t){ emit ps(k, t); }); 
            if (memcmp(r.data(), d.data(), d.size()) == 0) emit ls("✅ Write & Verify OK!"); else emit ls("❌ VERIFICATION FAILED!"); 
        } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); } }); 
    }
    
    void orc(bool f, bool v) {
        QString td = getActiveProjectDir();
        if (f && td.isEmpty()) { bool ok; QString name = QInputDialog::getText(this, "Create Project", "Enter Base Project Name:", QLineEdit::Normal, "MX_black", &ok); if (!ok || name.isEmpty()) return; QDir d(getRootPath() + "/projects"); if(!d.exists()) d.mkdir("."); d.mkdir(name); refreshProjectList(name); td = getActiveProjectDir(); }
        rt([this, f, v, td]() { 
            emit ls("[i] Connecting to MCU..."); QString e; QSslCertificate c = fc(e); 
            if (c.isNull()) { emit ls("⚠️ Cert Error: " + e); return; } 
            QString localTd = td; 
            if (f) { 
                fcrt = c; 
                vn = c.subjectInfo(QSslCertificate::CommonName).join(""); 
                emit ls("VIN FOUND: " + vn); 
                QString curName = QFileInfo(localTd).fileName(); 
                if (!curName.contains(vn) && !vn.isEmpty()) { 
                    QString newName = curName + "_" + vn; 
                    QDir parent = QFileInfo(localTd).dir(); 
                    if (parent.rename(curName, newName)) { 
                        emit ls("✅ Project Renamed: " + newName); 
                        QMetaObject::invokeMethod(this, "refreshProjectList", Qt::QueuedConnection, Q_ARG(QString, newName));
                        localTd = parent.absoluteFilePath(newName); 
                    } 
                } 
            } 
            QDateTime s = c.effectiveDate(); QDateTime n = c.expiryDate(); qint64 tdur = s.daysTo(n); qint64 dl = QDateTime::currentDateTime().daysTo(n); 
            emit ls(QString("CN: %1\nTotal: %2 d\nLeft: %3 d").arg(vn).arg(tdur).arg(dl)); 
            QString stat; if (f) { if (tdur > 700) { if(dl>0) stat="✅ Certificate is valid 2-year cert."; else stat="⚠️ Cert expired."; } else stat="⚠️ Cert duration weird."; } else if (!v) { if (dl > 300 && dl < 400) stat="✅ Valid ~1 year temporary cert. OK."; else stat="⚠️ Check days left!"; } else { if (c == fcrt) { stat="✅ Cert matches Original! SUCCESS."; } else stat="❌ Cert MISMATCH!"; }
            emit ls(stat);
            if (f && !localTd.isEmpty()) { QFile cf(localTd + "/cert_info.txt"); if(cf.open(QIODevice::WriteOnly | QIODevice::Append)) { cf.write(QString("Subject: %1\nValid: %2 - %3\n%4\n\n").arg(vn).arg(s.toString()).arg(n.toString()).arg(stat).toUtf8()); cf.close(); emit ls("[FILE] Info saved: " + localTd + "/cert_info.txt"); } }
        });
    }

    void oss() { 
        if (httpServer->isListening()) { 
            httpServer->close(); 
            bs->setText("Start HTTP Server"); 
            al("[HTTP] Stopped."); 
        } else { 
            httpServer->setRoot(httpRootPath);
            quint16 port = sbPort->value();
            if (httpServer->listen(QHostAddress::Any, port)) {
                bs->setText("Stop HTTP Server"); 
                mip = cmbIp->currentText();
                
                al(QString("[HTTP] Started :%1 in %2").arg(port).arg(httpRootPath)); 
                al("Serving on IP: " + mip); 
            } else {
                al("[HTTP] Failed to bind port!");
            }
        } 
    }

    void refreshIpList() {
        QString current = cmbIp->currentText();
        cmbIp->clear();
        cmbIp->addItem("192.168.90.125"); 
        foreach (const QHostAddress &a, QNetworkInterface::allAddresses()) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) {
                if (a.toString() != "192.168.90.125") cmbIp->addItem(a.toString());
            }
        }
        if (!current.isEmpty()) cmbIp->setCurrentText(current);
        else cmbIp->setCurrentIndex(0); 
    }

    void selHttpFolder() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select HTTP Root Folder", httpRootPath);
        if (!dir.isEmpty()) { httpRootPath = dir; lblHttpPath->setText(QFileInfo(dir).fileName() + "/"); lblHttpPath->setToolTip(dir); rf(); al("[UI] HTTP Root changed to: " + dir); }
    }

    void ocu() { nm->get(QNetworkRequest(QUrl(QString("http://%1:%2/status").arg(T_ADDR).arg(T_U_P)))); }
    void oru() { nm->get(QNetworkRequest(QUrl(QString("http://%1:%2/reset").arg(T_ADDR).arg(T_U_P)))); }
    void oim() { QString fn = cmf->currentText(); bool isCustom = rbCustom->isChecked(); QString targetUrl; if (isCustom) { targetUrl = txtCustomUrl->text(); if (targetUrl.isEmpty()) { al("Enter custom URL!"); return; } al("Install from Custom URL: " + targetUrl); } else { if (fn.isEmpty()) { al("Select file!"); return; } if (mip.isEmpty()) { al("Start Server!"); return; } targetUrl = QString("http://%1:%2/%3").arg(mip).arg(sbPort->value()).arg(fn); al("Install Local File: " + fn); }
        QString jsonTemplate = "{\"customer_version\": \"Pray for Ukraine\",\"map_download_file_md5\": \"GDebj0p86sSyUvXT/xWSBERaEFBOHmzlEBfq7lPWwYu9soP2fGj0Td3me4KQEwPRhxeo3wr0hWKYSBDbF8W5Dg==\",\"map_download_url\": \"%1\",\"modules_to_skip\":\"gtw,ape,192.168.90.105,games,modem\",\"vehicle_job_status_url\": \"http://192.168.90.125:80/jobs/1/signature/statuses\",\"verify_in_chunks\": \"false\"}"; QString jsonBody = jsonTemplate.arg(targetUrl); QString encodedJson = QUrl::toPercentEncoding(jsonBody); snd_h(encodedJson, targetUrl); }
    void snd_h(QString ej, QString du) { al("[CMD] Sending Handshake..."); QNetworkReply *r = nm->get(QNetworkRequest(QUrl(QString("http://%1:%2/override_handshake?%3").arg(T_ADDR).arg(T_U_P).arg(ej)))); connect(r, &QNetworkReply::finished, [this, r, du](){ if (r->error() == QNetworkReply::NoError) { QByteArray resp = r->readAll(); al("[CMD] Handshake Response: " + resp); if (resp.contains("status=ok")) { al("[CMD] Handshake OK. Sending Install..."); snd_i(du); } else { al("[CMD] Handshake Failed."); } } else { al("[CMD] Handshake Error: " + r->errorString()); } r->deleteLater(); }); }
    void snd_i(QString du) { QString urlStr = QString("http://%1:%2/install?%3").arg(T_ADDR).arg(T_U_P).arg(du); QNetworkReply *r = nm->get(QNetworkRequest(QUrl(urlStr))); connect(r, &QNetworkReply::finished, [this, r](){ if (r->error() == QNetworkReply::NoError) { QByteArray resp = r->readAll(); al("[CMD] Install Response: " + resp); if(resp.contains("status=ok")) { al("[CMD] Download initiated by vehicle..."); } } else { al("[CMD] Install Error: " + r->errorString()); } r->deleteLater(); }); }
    void nf(QNetworkReply *r) { if (r->url().path().contains("status") || r->url().path().contains("reset")) { if (r->error()) al("Err: " + r->errorString()); else al("Reply: " + r->readAll()); } }
    
    void rf() { cmf->clear(); QDir d(httpRootPath); al("-- SCANNING FILES in " + d.absolutePath() + " --"); QStringList f = d.entryList(QDir::Files); if (f.isEmpty()) al("EMPTY FOLDER"); else { al(QString("Found %1 files").arg(f.size())); cmf->addItems(f); } }
    
    // --- ПРАВКИ В РАБОТЕ С ПРОЕКТАМИ ---
    void initProjects() { 
        QDir d(getRootPath()); 
        if (!d.exists("projects")) {
            if(!d.mkdir("projects")) al("[ERR] Cannot create 'projects' folder at: " + d.absolutePath());
        }
        QDir projDir(getRootPath() + "/projects");
        QFileInfoList list = projDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
        QString lastProj = list.isEmpty() ? "" : list.first().fileName();
        refreshProjectList(lastProj); 
    }
    Q_INVOKABLE void refreshProjectList(QString selectName = "") { 
        cmbProjects->blockSignals(true); 
        QString current = selectName.isEmpty() ? cmbProjects->currentText() : selectName;
        cmbProjects->clear(); 
        QDir d(getRootPath() + "/projects"); 
        QStringList dirs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name); 
        cmbProjects->addItems(dirs); 
        if (!current.isEmpty()) cmbProjects->setCurrentText(current);
        cmbProjects->blockSignals(false); 
        refreshEncFileList();
    }
    void refreshEncFileList() {
        cmbEncFiles->blockSignals(true);
        cmbEncFiles->clear();
        QString td = getActiveProjectDir();
        if (!td.isEmpty()) {
            QDir d(td);
            QStringList files = d.entryList(QStringList() << "*.bin", QDir::Files);
            cmbEncFiles->addItems(files);
        }
        cmbEncFiles->blockSignals(false);
    }
    void createProject() { bool ok; QString name = QInputDialog::getText(this, "New Project", "Name:", QLineEdit::Normal, "MX_New", &ok); if (ok && !name.isEmpty()) { QDir d(getRootPath() + "/projects"); if(!d.exists()) d.mkdir("."); if(d.mkdir(name)) { refreshProjectList(name); al("Project Created: " + name); } else { al("[ERR] Failed to create project folder!"); } } }
    void deleteProject() { QString name = cmbProjects->currentText(); if (name.isEmpty()) return; if(QMessageBox::Yes == QMessageBox::question(this, "Delete", "Delete project " + name + " and ALL files?", QMessageBox::Yes|QMessageBox::No)) { QDir d(getRootPath() + "/projects/" + name); d.removeRecursively(); refreshProjectList(); al("Project Deleted."); } }
    void openProjectDir() { QString name = cmbProjects->currentText(); if (name.isEmpty()) return; QString path = getRootPath() + "/projects/" + name; QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }
    QString getActiveProjectDir() { QString name = cmbProjects->currentText(); if (name.isEmpty()) return ""; return getRootPath() + "/projects/" + name; }
    QString getProjFile(QString name) { QString d = getActiveProjectDir(); if (d.isEmpty()) return QFileDialog::getSaveFileName(this, "Save", name); return d + "/" + name; }
    QSslCertificate fc(QString &lo) {
        QSslSocket socket;
        socket.setPeerVerifyMode(QSslSocket::VerifyNone);
        socket.connectToHostEncrypted(T_ADDR, T_S_P);
        if (socket.waitForEncrypted(5000)) {
            return socket.peerCertificate();
        } else {
            if (!socket.peerCertificate().isNull()) return socket.peerCertificate();
            lo = socket.errorString();
            return QSslCertificate();
        }
    }
    void rt(std::function<void()> f) { std::thread([f, this](){ if(f) f(); emit ps(0, 0); }).detach(); }
    uint32_t ghi(QString t, uint32_t d) { bool o; QString x = QInputDialog::getText(this, t, "Hex Value (0x..):", QLineEdit::Normal, QString::number(d, 16), &o); return o ? x.toUInt(nullptr, 16) : 0; }
    
    void oSafeRead() {
        QString p = getProjFile("safe_dump.bin");
        uint32_t total_size = sbSafeSize->value();
        uint32_t num_parts = sbSafeParts->value();
        int req_matches = sbSafeMatches->value();
        if(p.isEmpty()) return;

        rt([this, p, total_size, num_parts, req_matches]() {
            try {
                stopSafeOp = false;
                QMetaObject::invokeMethod(btnSafeStop, "setEnabled", Q_ARG(bool, true));
                QMetaObject::invokeMethod(btnSafeRead, "setEnabled", Q_ARG(bool, false));
                QMetaObject::invokeMethod(btnSafeWrite, "setEnabled", Q_ARG(bool, false));

                QByteArray final_data;
                for (uint32_t i = 0; i < num_parts; i++) {
                    if (stopSafeOp) break;
                    uint32_t start = i * (total_size / num_parts);
                    uint32_t size = (i == num_parts - 1) ? (total_size - start) : (total_size / num_parts);
                    emit ls(QString("\n>>> READ PART %1/%2").arg(i+1).arg(num_parts));
                    QByteArray part = stableRead(start, size, req_matches);
                    if (part.isEmpty() && size > 0) { emit ls("❌ FAILED to read part."); break; }
                    final_data.append(part);
                }

                if (!stopSafeOp) {
                    QFile f(p);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(final_data);
                        emit ls("✅ Safe Read Done. Saved to: " + p);
                    } else { emit ls("❌ Error saving file!"); }
                } else { emit ls("⚠️ Operation Stopped."); }
            } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); }
            QMetaObject::invokeMethod(btnSafeStop, "setEnabled", Q_ARG(bool, false));
            QMetaObject::invokeMethod(btnSafeRead, "setEnabled", Q_ARG(bool, true));
            QMetaObject::invokeMethod(btnSafeWrite, "setEnabled", Q_ARG(bool, true));
        });
    }

    void oSafeWrite() {
        QString p = QFileDialog::getOpenFileName(this, "Select File to Write");
        if(p.isEmpty()) return;
        if(QMessageBox::question(this, "Confirm", "Overwrite flash?") != QMessageBox::Yes) return;

        uint32_t num_parts = sbSafeParts->value();
        int req_matches = sbSafeMatches->value();

        rt([this, p, num_parts, req_matches]() {
            try {
                stopSafeOp = false;
                QMetaObject::invokeMethod(btnSafeStop, "setEnabled", Q_ARG(bool, true));
                QMetaObject::invokeMethod(btnSafeRead, "setEnabled", Q_ARG(bool, false));
                QMetaObject::invokeMethod(btnSafeWrite, "setEnabled", Q_ARG(bool, false));

                QFile f(p);
                if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open file");
                QByteArray file_data = f.readAll();
                uint32_t total_size = file_data.size();
                bool writeSuccess = true;
                for (uint32_t i = 0; i < num_parts; i++) {
                    if (stopSafeOp) break;
                    uint32_t start = i * (total_size / num_parts);
                    uint32_t size = (i == num_parts - 1) ? (total_size - start) : (total_size / num_parts);
                    QByteArray part_data = file_data.mid(start, size);
                    emit ls(QString("\n>>> WRITE PART %1/%2").arg(i+1).arg(num_parts));

                    bool partSuccess = false;
                    for (int attempt = 1; !stopSafeOp; ++attempt) {
                        emit ls(QString(" Attempt %1: Erasing...").arg(attempt));
                        uint32_t curr = start;
                        while (curr < start + size) {
                            if (stopSafeOp) break;
                            dr->ser(curr, std::min((uint32_t)65536, start + size - curr), nullptr); // Erase block (64KB)
                            curr += 65536;
                        }

                        emit ls(" Writing...");
                        uint32_t done = 0;
                        while (done < size) {
                            if (stopSafeOp) break;
                            uint32_t n = std::min((uint32_t)256, size - done);
                            std::vector<uint8_t> page(part_data.constData() + done, part_data.constData() + done + n);
                            dr->pb(start + done, page, nullptr);
                            done += n;
                            if (done % 4096 == 0) emit ps(done, size);
                        }

                        bool verifyOk = false;
                        for (int verifyAttempt = 1; verifyAttempt <= 2; ++verifyAttempt) {
                            if (stopSafeOp) break;
                            emit ls(QString(" Verifying %1/2...").arg(verifyAttempt));
                            QByteArray verify = stableRead(start, size, req_matches, part_data, true);
                            if (!verify.isEmpty()) { verifyOk = true; break; }
                        }
                        if (verifyOk) { partSuccess = true; break; }
                        if (stopSafeOp) break;
                        emit ls(" ❌ Verification Mismatch.");
                    }
                    if (stopSafeOp) break;
                    if (!partSuccess) { writeSuccess = false; emit ls("❌ FAILED to write part."); break; }
                }
                if (stopSafeOp) emit ls("⚠️ Operation Stopped.");
                else if (writeSuccess) emit ls("✅ Safe Write Done.");
                else emit ls("❌ Safe Write Failed.");
            } catch(std::exception &e) { emit ls(QString("[ERR] %1").arg(e.what())); }
            QMetaObject::invokeMethod(btnSafeStop, "setEnabled", Q_ARG(bool, false));
            QMetaObject::invokeMethod(btnSafeRead, "setEnabled", Q_ARG(bool, true));
            QMetaObject::invokeMethod(btnSafeWrite, "setEnabled", Q_ARG(bool, true));
        });
    }

    int firstDiffOffset(const QByteArray &expected, const QByteArray &actual) {
        int n = std::min(expected.size(), actual.size());
        for (int i = 0; i < n; ++i) {
            if (expected.at(i) != actual.at(i)) return i;
        }
        return expected.size() == actual.size() ? -1 : n;
    }

    QByteArray stableRead(uint32_t start, uint32_t size, int reqMatches, const QByteArray &target = QByteArray(), bool stopOnTargetMismatch = false) {
        QMap<QByteArray, int> hashCounts;
        QByteArray targetHash = target.isEmpty() ? QByteArray() : QCryptographicHash::hash(target, QCryptographicHash::Md5);

        for (int attempts = 1; attempts <= 15; attempts++) {
            if (stopSafeOp) return QByteArray();
            auto rd = dr->rd(start, size, [this](int k, int t){ emit ps(k, t); });
            QByteArray data((const char*)rd.data(), rd.size());
            QByteArray h = QCryptographicHash::hash(data, QCryptographicHash::Md5);
            
            if (!targetHash.isEmpty() && h != targetHash) {
                emit ls(QString("  T%1: Mismatch").arg(attempts));
                if (stopOnTargetMismatch) {
                    int diff = firstDiffOffset(target, data);
                    if (diff >= 0) {
                        emit ls(QString("    First diff @ 0x%1: expected 0x%2, got 0x%3")
                            .arg(start + (uint32_t)diff, 8, 16, QChar('0')).toUpper()
                            .arg((uint8_t)target.at(diff), 2, 16, QChar('0')).toUpper()
                            .arg((uint8_t)data.at(diff), 2, 16, QChar('0')).toUpper());
                    }
                    return QByteArray();
                }
                continue;
            }
            hashCounts[h]++;
            emit ls(QString("  T%1: Match %2/%3").arg(attempts).arg(hashCounts[h]).arg(reqMatches));
            if (hashCounts[h] >= reqMatches) return data;
        }
        return QByteArray();
    }

    // --- UI SETUP ---
    void ui() {
        setWindowTitle("Tigard Maps Installer - Open Source");
        resize(1280, 800); 

        QWidget *c = new QWidget; c->setObjectName("CentralWidget"); setCentralWidget(c); 
        
        QString style = R"(
            QWidget { font-family: -apple-system, Helvetica, Arial, sans-serif; background-color: #2b2b2b; color: #f0f0f0; }
            QWidget#CentralWidget { background-color: #2b2b2b; }
            QGroupBox { font-weight: bold; border: 1px solid #444; border-radius: 6px; margin-top: 12px; background-color: #333; padding: 10px; color: #fff; }
            QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #ddd; }
            QPushButton { background-color: #444; border: 1px solid #555; border-radius: 4px; padding: 6px 12px; font-weight: 600; color: #fff; }
            QPushButton:hover { background-color: #555; border-color: #0078d7; }
            QPushButton:pressed { background-color: #666; }
            QLineEdit, QComboBox, QSpinBox { background-color: #1e1e1e; border: 1px solid #555; border-radius: 4px; padding: 4px; color: #fff; min-height: 20px; selection-background-color: #0078d7; }
            QComboBox QAbstractItemView { background-color: #2b2b2b; color: #fff; selection-background-color: #0078d7; }
            QLabel { color: #e0e0e0; }
            QTextEdit { font-family: 'JetBrains Mono', 'Consolas', monospace; font-size: 11pt; border: 1px solid #444; background: #111; color: #0f0; }
            QTabWidget::pane { border: 1px solid #444; background: #333; }
            QTabBar::tab { background: #2b2b2b; color: #aaa; padding: 8px 16px; border: 1px solid #444; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 2px; }
            QTabBar::tab:selected { background: #3c3c3c; color: #fff; border-bottom: 2px solid #0078d7; }
            QTabBar::tab:hover { background: #333; color: #fff; }
            QProgressBar { border: 1px solid #555; border-radius: 5px; text-align: center; background-color: #1a1a1a; color: #fff; font-weight: bold; }
            QProgressBar::chunk { background-color: #0078d7; width: 20px; }
            QCheckBox { color: #e0e0e0; }
            QRadioButton { color: #e0e0e0; }
        )";
        c->setStyleSheet(style);
        
        QVBoxLayout *mainLayout = new QVBoxLayout(c);
        mainLayout->setContentsMargins(15, 15, 15, 15);
        mainLayout->setSpacing(10);

        // --- 1. TOP HEADER ---
        QHBoxLayout *topBar = new QHBoxLayout;
        topBar->addWidget(new QLabel("Project:"));
        cmbProjects = new QComboBox; cmbProjects->setMinimumWidth(200);
        topBar->addWidget(cmbProjects);
        connect(cmbProjects, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GuiMain::refreshEncFileList);
        QPushButton *btnNew = new QPushButton("+"); btnNew->setFixedWidth(40); connect(btnNew, &QPushButton::clicked, this, &GuiMain::createProject); topBar->addWidget(btnNew);
        QPushButton *btnOpn = new QPushButton("📂"); btnOpn->setFixedWidth(40); connect(btnOpn, &QPushButton::clicked, this, &GuiMain::openProjectDir); topBar->addWidget(btnOpn);

        topBar->addSpacing(30);
        QLabel *sep = new QLabel("|"); sep->setStyleSheet("color: #555;"); topBar->addWidget(sep);
        topBar->addSpacing(30);

        topBar->addWidget(new QLabel("Tigard:"));
        ci = new QComboBox; ci->addItems({"A","B"}); ci->setCurrentIndex(1); ci->setFixedWidth(60); topBar->addWidget(ci);
        cf = new QComboBox; cf->addItem("1M",1000000); cf->addItem("6M",6000000); cf->addItem("15M",15000000); topBar->addWidget(cf);
        ck = new QCheckBox("Fast"); topBar->addWidget(ck);
        QPushButton *bc = new QPushButton("Connect"); connect(bc, &QPushButton::clicked, this, &GuiMain::oc); topBar->addWidget(bc);
        QPushButton *bj = new QPushButton("ID"); connect(bj, &QPushButton::clicked, this, &GuiMain::oj); topBar->addWidget(bj);
        
        topBar->addStretch();
        lblTigardId = new QLabel("Searching...");
        lblTigardId->setStyleSheet("font-family:monospace; color:#888;");
        topBar->addWidget(lblTigardId);

        mainLayout->addLayout(topBar);

        // --- NETWORK BAR ---
        QHBoxLayout *netBar = new QHBoxLayout;
        netBar->setContentsMargins(5, 5, 5, 5);
        QLabel *lblNet = new QLabel("Network Status:"); lblNet->setStyleSheet("font-weight: bold; color: #ccc;");
        netBar->addWidget(lblNet);
        netBar->addSpacing(10);
        netBar->addWidget(new QLabel("GW (102):")); lg = new QLabel; lg->setFixedSize(16,16); lg->setStyleSheet("background:gray; border-radius:8px; border:1px solid #000;"); netBar->addWidget(lg);
        netBar->addSpacing(15); 
        netBar->addWidget(new QLabel("CID (100):")); lc = new QLabel; lc->setFixedSize(16,16); lc->setStyleSheet("background:gray; border-radius:8px; border:1px solid #000;"); netBar->addWidget(lc);
        netBar->addSpacing(30);
        QPushButton *bu1 = new QPushButton("Unlock Port (Single)"); connect(bu1, &QPushButton::clicked, this, &GuiMain::sup); netBar->addWidget(bu1);
        bu = new QPushButton("Cycle Unlock"); connect(bu, &QPushButton::clicked, this, &GuiMain::ouc); netBar->addWidget(bu);
        netBar->addStretch();
        mainLayout->addLayout(netBar);

        // --- 2. MAIN SPLITTER ---
        QSplitter *splitter = new QSplitter(Qt::Horizontal);
        splitter->setHandleWidth(2);
        splitter->setStyleSheet("QSplitter::handle { background-color: #444; }");

        QTabWidget *tabs = new QTabWidget;
        
        // --- TAB 1: INSTALLER ---
        QWidget *tabInst = new QWidget;
        QVBoxLayout *lInst = new QVBoxLayout(tabInst);
        
        QGroupBox *gm = new QGroupBox("Map Installer"); 
        QVBoxLayout *vlm = new QVBoxLayout(gm); 
        QHBoxLayout *row1 = new QHBoxLayout;
        btnSelectHttp = new QPushButton("Select Folder"); connect(btnSelectHttp, &QPushButton::clicked, this, &GuiMain::selHttpFolder);
        lblHttpPath = new QLabel(QFileInfo(httpRootPath).fileName()); 
        row1->addWidget(btnSelectHttp); row1->addWidget(lblHttpPath); row1->addStretch();
        vlm->addLayout(row1);
        QHBoxLayout *row2 = new QHBoxLayout;
        
        row2->addWidget(new QLabel("Server IP:"));
        cmbIp = new QComboBox; cmbIp->setMinimumWidth(150); cmbIp->setEditable(true);
        row2->addWidget(cmbIp);
        vlm->addLayout(row2);

        QHBoxLayout *rowPort = new QHBoxLayout;
        rowPort->addWidget(new QLabel("Server Port:"));
        sbPort = new QSpinBox; sbPort->setRange(1, 65535); sbPort->setValue(8000);
        sbPort->setMinimumWidth(100);
        rowPort->addWidget(sbPort);
        
        bs = new QPushButton("Start HTTP Server"); connect(bs,&QPushButton::clicked,this,&GuiMain::oss); 
        rowPort->addWidget(bs);
        
        QPushButton *mcs = new QPushButton("Chk Status"); connect(mcs,&QPushButton::clicked,this,&GuiMain::ocu); 
        rowPort->addWidget(mcs);
        
        QPushButton *mrs = new QPushButton("Reset"); connect(mrs,&QPushButton::clicked,this,&GuiMain::oru); 
        rowPort->addWidget(mrs);
        rowPort->addStretch();
        vlm->addLayout(rowPort);
        
        QHBoxLayout *row3 = new QHBoxLayout;
        QButtonGroup *bg = new QButtonGroup(this); 
        rbLocal = new QRadioButton("Local File"); rbLocal->setChecked(true); bg->addButton(rbLocal);
        rbCustom = new QRadioButton("URL"); bg->addButton(rbCustom);
        row3->addWidget(rbLocal); row3->addWidget(rbCustom); row3->addStretch();
        vlm->addLayout(row3);
        QHBoxLayout *row4 = new QHBoxLayout;
        cmf = new QComboBox; txtCustomUrl = new QLineEdit; txtCustomUrl->setPlaceholderText("http://..."); txtCustomUrl->setVisible(false);
        row4->addWidget(cmf, 1); row4->addWidget(txtCustomUrl, 1);
        QPushButton *brf = new QPushButton("↻"); brf->setFixedWidth(40); connect(brf,&QPushButton::clicked,this,&GuiMain::rf); 
        row4->addWidget(brf);
        connect(rbLocal, &QRadioButton::toggled, [this](bool c){ cmf->setVisible(c); txtCustomUrl->setVisible(!c); }); 
        vlm->addLayout(row4);
        QPushButton *bin = new QPushButton("INSTALL MAPS"); bin->setMinimumHeight(60);
        bin->setStyleSheet("QPushButton { background-color: #ff9800; color: #000; font-size: 16px; border: none; font-weight: 900; } QPushButton:hover { background-color: #ffb74d; } QPushButton:pressed { background-color: #f57c00; }"); 
        connect(bin,&QPushButton::clicked,this,&GuiMain::oim); 
        vlm->addWidget(bin);
        QHBoxLayout *row6 = new QHBoxLayout;
        lhp = new QLabel("Upload: 0%"); hp = new QProgressBar; hp->setRange(0,100); 
        row6->addWidget(lhp); row6->addWidget(hp, 1);
        vlm->addLayout(row6);
        lInst->addWidget(gm);
        lInst->addStretch();

        // --- TAB 2: MEMORY ---
        QWidget *tabMem = new QWidget;
        QVBoxLayout *lMem = new QVBoxLayout(tabMem);
        QGroupBox *gf = new QGroupBox("Full Flash (32MB)"); QHBoxLayout *hf = new QHBoxLayout(gf);
        QPushButton *br = new QPushButton("Read Full"); connect(br, &QPushButton::clicked, this, &GuiMain::ofr); hf->addWidget(br);
        QPushButton *bw = new QPushButton("Write Full"); connect(bw, &QPushButton::clicked, this, &GuiMain::ofw); hf->addWidget(bw); lMem->addWidget(gf);
        QGroupBox *ga = new QGroupBox("Address Ops"); QGridLayout *gla = new QGridLayout(ga);
        QPushButton *b1=new QPushButton("Read -> File"); connect(b1,&QPushButton::clicked,this,&GuiMain::oarf); gla->addWidget(b1,0,0);
        QPushButton *b2=new QPushButton("Read -> Log"); connect(b2,&QPushButton::clicked,this,&GuiMain::oarl); gla->addWidget(b2,0,1);
        QPushButton *b3=new QPushButton("Write Addr"); connect(b3,&QPushButton::clicked,this,&GuiMain::oaw); gla->addWidget(b3,1,0);
        QPushButton *b4=new QPushButton("Compare"); connect(b4,&QPushButton::clicked,this,&GuiMain::oac); gla->addWidget(b4,1,1); lMem->addWidget(ga);

        QGroupBox *gs = new QGroupBox("Safe Full Read/Write (Chunked)");
        QVBoxLayout *lsv = new QVBoxLayout(gs);
        QHBoxLayout *rs1 = new QHBoxLayout;
        rs1->addWidget(new QLabel("Size (Bytes):")); sbSafeSize = new QSpinBox; sbSafeSize->setRange(1, 1024*1024*1024); sbSafeSize->setValue(33554432); sbSafeSize->setSingleStep(1024*1024); rs1->addWidget(sbSafeSize);
        rs1->addWidget(new QLabel("Parts:")); sbSafeParts = new QSpinBox; sbSafeParts->setRange(1, 1024); sbSafeParts->setValue(32); rs1->addWidget(sbSafeParts);
        rs1->addWidget(new QLabel("Match:")); sbSafeMatches = new QSpinBox; sbSafeMatches->setRange(1, 5); sbSafeMatches->setValue(2); rs1->addWidget(sbSafeMatches);
        lsv->addLayout(rs1);
        QHBoxLayout *rs2 = new QHBoxLayout;
        btnSafeRead = new QPushButton("SAFE READ"); btnSafeRead->setStyleSheet("color: #4CAF50; font-weight: bold;"); connect(btnSafeRead, &QPushButton::clicked, this, &GuiMain::oSafeRead);
        btnSafeWrite = new QPushButton("SAFE WRITE"); btnSafeWrite->setStyleSheet("color: #FF9800; font-weight: bold;"); connect(btnSafeWrite, &QPushButton::clicked, this, &GuiMain::oSafeWrite);
        btnSafeStop = new QPushButton("STOP"); btnSafeStop->setStyleSheet("color: #f44336; font-weight: bold;"); btnSafeStop->setEnabled(false); connect(btnSafeStop, &QPushButton::clicked, [this](){ stopSafeOp = true; });
        rs2->addWidget(btnSafeRead); rs2->addWidget(btnSafeWrite); rs2->addWidget(btnSafeStop);
        lsv->addLayout(rs2);
        lMem->addWidget(gs);

        lMem->addStretch();

        // --- TAB 3: SECURITY ---
        QWidget *tabSec = new QWidget;
        QVBoxLayout *lSec = new QVBoxLayout(tabSec);
        QGroupBox *gv = new QGroupBox("Certificates"); QVBoxLayout *hv = new QVBoxLayout(gv);
        QPushButton *v1 = new QPushButton("1. Read Original (2yr)"); connect(v1,&QPushButton::clicked,this,[this](){orc(true,false);}); hv->addWidget(v1);
        QPushButton *v2 = new QPushButton("2. Verify Temp (1yr)"); connect(v2,&QPushButton::clicked,this,[this](){orc(false,false);}); hv->addWidget(v2);
        QPushButton *v3 = new QPushButton("3. Verify Write"); connect(v3,&QPushButton::clicked,this,[this](){orc(false,true);}); hv->addWidget(v3); lSec->addWidget(gv);
        QGroupBox *ge = new QGroupBox("ENC Data (raw files)"); QVBoxLayout *he = new QVBoxLayout(ge);
        QPushButton *es = new QPushButton("Smart Read ENC (Auto x3)"); es->setStyleSheet("color: #66bb6a;"); connect(es,&QPushButton::clicked,this,&GuiMain::ore); he->addWidget(es);
        QPushButton *ee = new QPushButton("Erase ENC"); ee->setStyleSheet("color: #ef5350;"); connect(ee,&QPushButton::clicked,this,&GuiMain::oee); he->addWidget(ee);
        
        QPushButton *eck = new QPushButton("Check File Validity"); connect(eck, &QPushButton::clicked, this, &GuiMain::och); he->addWidget(eck);

        QHBoxLayout *hwe = new QHBoxLayout;
        cmbEncFiles = new QComboBox; hwe->addWidget(cmbEncFiles, 1);
        btnWriteSelected = new QPushButton("Write Selected"); connect(btnWriteSelected, &QPushButton::clicked, this, &GuiMain::ope); hwe->addWidget(btnWriteSelected);
        he->addLayout(hwe);

        QPushButton *ew = new QPushButton("Write From File..."); ew->setStyleSheet("color: #42a5f5;"); connect(ew,&QPushButton::clicked,this,&GuiMain::ope); he->addWidget(ew); 
        lSec->addWidget(ge);
        lSec->addStretch();

        tabs->addTab(tabInst, "Installer");
        tabs->addTab(tabMem, "Memory");
        tabs->addTab(tabSec, "Security");

        splitter->addWidget(tabs);

        // RIGHT SIDE: LOG
        tl = new QTextEdit; tl->setReadOnly(true);
        splitter->addWidget(tl);
        
        splitter->setSizes(QList<int>() << 500 << 780);
        splitter->setStretchFactor(1, 1);

        mainLayout->addWidget(splitter);

        // --- 3. BOTTOM STATUS ---
        QHBoxLayout *h_stat = new QHBoxLayout; 
        pb = new QProgressBar; pb->setRange(0,100); pb->setFixedHeight(15);
        lp = new QLabel("0%"); 
        h_stat->addWidget(pb); h_stat->addWidget(lp); 
        mainLayout->addLayout(h_stat);

        connect(this, &GuiMain::ls, this, &GuiMain::al); connect(this, &GuiMain::ps, this, &GuiMain::up);
        
        connect(httpServer, &NativeHttpServer::uploadProgress, this, [this](float pct, double sent, double tot, double spd, int eta){
             hp->setValue((int)pct);
             lhp->setText(QString("Upload: %1% | %2/%3 MB | %4 MB/s | ETA: %5:%6")
                          .arg(pct, 0, 'f', 1).arg(sent, 0, 'f', 1).arg(tot, 0, 'f', 1)
                          .arg(spd, 0, 'f', 2).arg(eta/60,2,10,QChar('0')).arg(eta%60,2,10,QChar('0')));
             
             if (pct >= 100.0) {
                 static QElapsedTimer lastBeep;
                 if (!lastBeep.isValid() || lastBeep.hasExpired(5000)) {
                    al("!!! MAP DOWNLOAD FINISHED - REBOOT ICE !!!");
                    for(int i=0; i<3; i++) QApplication::beep();
                    lastBeep.start();
                 }
             }
        });
    }

private:
    IO_Dr *dr; QNetworkAccessManager *nm; 
    NativeHttpServer *httpServer; 
    QTimer *pt; QTimer *ut;
    QUdpSocket *us;
    QTextEdit *tl; QProgressBar *pb, *hp; QLabel *lp, *lhp, *lg, *lc; 
    QComboBox *ci, *cf, *cmf, *cmbProjects, *cmbIp, *cmbEncFiles; QCheckBox *ck; QPushButton *bs, *bu, *btnWriteSelected;
    QSpinBox *sbPort, *sbSafeSize, *sbSafeParts, *sbSafeMatches;
    QPushButton *btnSafeRead, *btnSafeWrite, *btnSafeStop;
    std::atomic<bool> stopSafeOp{false};
    QRadioButton *rbLocal; QRadioButton *rbCustom; QLineEdit *txtCustomUrl;
    QPushButton *btnSelectHttp; QLabel *lblHttpPath, *lblTigardId; QString httpRootPath;
    bool cn = false; QSslCertificate fcrt; QString vn, mip;

    bool verifyEncFiles() {
        QString td = getActiveProjectDir();
        if (td.isEmpty()) return false;
        QDir dir(td);
        QStringList filters; filters << "*_valid.bin";
        QStringList files = dir.entryList(filters, QDir::Files);
        int validCount = 0;
        for (const QString &fn : files) {
            QFile f(td + "/" + fn);
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray raw = f.readAll();
                if (raw.size() == E_L && (unsigned char)raw[0] == 0x41 && (unsigned char)raw[1] == 0x50 && (unsigned char)raw[2] == 0x4F && (unsigned char)raw[3] == 0x42) {
                    validCount++;
                }
                f.close();
            }
        }
        return (validCount >= 3);
    }
};

int main(int c, char *v[]) {
#ifndef Q_OS_WIN
    // --- FIX FOR LINUX APPIMAGE SSL ---
    qputenv("QT_SSL_USE_TEMPORARY_KEYCHAIN", "1");
    QByteArray certPath = qgetenv("SSL_CERT_FILE");
    if (certPath.isEmpty()) {
        qputenv("SSL_CERT_FILE", "/etc/ssl/certs/ca-certificates.crt");
        qputenv("SSL_CERT_DIR", "/etc/ssl/certs");
    }
#endif
    // ----------------------------------
    QApplication a(c, v);
    GuiMain w; w.show();
    return a.exec();
}
#include "main.moc"
