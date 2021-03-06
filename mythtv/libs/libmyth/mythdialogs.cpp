
#include <iostream>
#include <algorithm>
using namespace std;

#include <QCoreApplication>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QLayout>
#include <QRegExp>
#include <QImageReader>
#include <QLabel>
#include <QPixmap>
#include <QKeyEvent>
#include <QFrame>
#include <QPaintEvent>
#include <QPainter>
#include <QProgressBar>

#ifdef QWS
#include <qwindowsystem_qws.h>
#endif

#include "uitypes.h"
#include "uilistbtntype.h"
#include "xmlparse.h"
#include "mythdialogs.h"
#include "lcddevice.h"
#include "mythdbcon.h"
#include "mythfontproperties.h"
#include "mythuihelper.h"
#include "mythverbose.h"
#include "mythcorecontext.h"

#ifdef USING_MINGW
#undef LoadImage
#endif

/** \class MythDialog
 *  \brief Base dialog for most dialogs in MythTV using the old UI
 */

MythDialog::MythDialog(MythMainWindow *parent, const char *name, bool setsize)
    : QFrame(parent), rescode(kDialogCodeAccepted)
{
    setObjectName(name);
    if (!parent)
    {
        VERBOSE(VB_IMPORTANT, "Trying to create a dialog without a parent.");
        return;
    }

    in_loop = false;
    MythUIHelper *ui = GetMythUI();

    ui->GetScreenSettings(xbase, screenwidth, wmult,
                          ybase, screenheight, hmult);

    defaultBigFont = ui->GetBigFont();
    defaultMediumFont = ui->GetMediumFont();
    defaultSmallFont = ui->GetSmallFont();

    setFont(defaultMediumFont);

    if (setsize)
    {
        move(0, 0);
        setFixedSize(QSize(screenwidth, screenheight));
        GetMythUI()->ThemeWidget(this);
    }

    setAutoFillBackground(true);

    parent->attach(this);
    m_parent = parent;
}

MythDialog::~MythDialog()
{
    TeardownAll();
}

void MythDialog::deleteLater(void)
{
    hide();
    TeardownAll();
    QFrame::deleteLater();
}

void MythDialog::TeardownAll(void)
{
    if (m_parent)
    {
        m_parent->detach(this);
        m_parent = NULL;
    }
}

void MythDialog::setNoErase(void)
{
}

bool MythDialog::onMediaEvent(MythMediaDevice*)
{
    return false;
}



void MythDialog::Show(void)
{
    show();
}

void MythDialog::setResult(DialogCode r)
{
    if ((r < kDialogCodeRejected) ||
        ((kDialogCodeAccepted < r) && (r < kDialogCodeListStart)))
    {
        VERBOSE(VB_IMPORTANT, QString(
                    "Programmer Error: MythDialog::setResult(%1) "
                    "called with invalid DialogCode").arg(r));
    }

    rescode = r;
}

void MythDialog::done(int r)
{
    hide();
    setResult((DialogCode) r);
    close();
}

void MythDialog::AcceptItem(int i)
{
    if (i < 0)
    {
        VERBOSE(VB_IMPORTANT,
                QString("Programmer Error: MythDialog::AcceptItem(%1) "
                        "called with negative index").arg(i));
        reject();
        return;
    }

    done((DialogCode)((int)kDialogCodeListStart + (int)i));
}

int MythDialog::CalcItemIndex(DialogCode code)
{
    return (int)code - (int)kDialogCodeListStart;
}

void MythDialog::accept()
{
    done(Accepted);
}

void MythDialog::reject()
{
    done(Rejected);
}

DialogCode MythDialog::exec(void)
{
    if (in_loop)
    {
        VERBOSE(VB_IMPORTANT, "MythDialog::exec: Recursive call detected.");
        return kDialogCodeRejected;
    }

    setResult(kDialogCodeRejected);

    Show();

    in_loop = true;

    QEventLoop eventLoop;
    connect(this, SIGNAL(leaveModality()), &eventLoop, SLOT(quit()));
    eventLoop.exec();

    DialogCode res = result();

    return res;
}

void MythDialog::hide(void)
{
    if (isHidden())
        return;

    // Reimplemented to exit a modal when the dialog is hidden.
    QWidget::hide();
    if (in_loop)
    {
        in_loop = false;
        emit leaveModality();
    }
}

void MythDialog::keyPressEvent( QKeyEvent *e )
{
    bool handled = false;
    QStringList actions;

    handled = GetMythMainWindow()->TranslateKeyPress("qt", e, actions);

    for (int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];
        handled = true;

        if (action == "ESCAPE")
            reject();
        else if (action == "UP" || action == "LEFT")
        {
            if (focusWidget() &&
                (focusWidget()->focusPolicy() == Qt::StrongFocus ||
                    focusWidget()->focusPolicy() == Qt::WheelFocus))
            {
            }
            else
                focusNextPrevChild(false);
        }
        else if (action == "DOWN" || action == "RIGHT")
        {
            if (focusWidget() &&
                (focusWidget()->focusPolicy() == Qt::StrongFocus ||
                    focusWidget()->focusPolicy() == Qt::WheelFocus))
            {
            }
            else
                focusNextPrevChild(true);
        }
        else if (action == "MENU")
            emit menuButtonPressed();
        else
            handled = false;
    }
}

/** \class MythPopupBox
 *  \brief Child of MythDialog used for most popup menus in MythTV
 *
 *  Most users of this class just call one of the static functions
 *  These create a dialog and block until it returns with a DialogCode.
 *
 *  When creating an instance yourself and using ExecPopup() or
 *  ShowPopup() you can optionally pass it a target and slot for
 *  the popupDone(int) signal. It will be sent with the DialogCode
 *  that the exec function returns, except it is cast to an int.
 *  This is most useful for ShowPopup() which doesn't block or
 *  return the result() when the popup is finished.
 */

MythPopupBox::MythPopupBox(MythMainWindow *parent, const char *name)
            : MythDialog(parent, name, false)
{
    float wmult, hmult;

    if (gCoreContext->GetNumSetting("UseArrowAccels", 1))
        arrowAccel = true;
    else
        arrowAccel = false;

    GetMythUI()->GetScreenSettings(wmult, hmult);

    setLineWidth(3);
    setMidLineWidth(3);
    setFrameShape(QFrame::Panel);
    setFrameShadow(QFrame::Raised);
    setPalette(parent->palette());
    popupForegroundColor = palette().color(foregroundRole());
    setFont(parent->font());

    hpadding = gCoreContext->GetNumSetting("PopupHeightPadding", 120);
    wpadding = gCoreContext->GetNumSetting("PopupWidthPadding", 80);

    vbox = new QVBoxLayout(this);
    vbox->setMargin((int)(10 * hmult));

    setAutoFillBackground(true);
    setWindowFlags(Qt::FramelessWindowHint);
}

MythPopupBox::MythPopupBox(MythMainWindow *parent, bool graphicPopup,
                           QColor popupForeground, QColor popupBackground,
                           QColor popupHighlight, const char *name)
            : MythDialog(parent, name, false)
{
    float wmult, hmult;

    if (gCoreContext->GetNumSetting("UseArrowAccels", 1))
        arrowAccel = true;
    else
        arrowAccel = false;

    GetMythUI()->GetScreenSettings(wmult, hmult);

    setLineWidth(3);
    setMidLineWidth(3);
    setFrameShape(QFrame::Panel);
    setFrameShadow(QFrame::Raised);
    setFrameStyle(QFrame::Box | QFrame::Plain);
    setPalette(parent->palette());
    setFont(parent->font());

    hpadding = gCoreContext->GetNumSetting("PopupHeightPadding", 120);
    wpadding = gCoreContext->GetNumSetting("PopupWidthPadding", 80);

    vbox = new QVBoxLayout(this);
    vbox->setMargin((int)(10 * hmult));

    if (!graphicPopup)
    {
        QPalette palette;
        palette.setColor(backgroundRole(), popupBackground);
        setPalette(palette);
    }
    else
        GetMythUI()->ThemeWidget(this);

    QPalette palette;
    palette.setColor(foregroundRole(), popupHighlight);
    setPalette(palette);

    popupForegroundColor = popupForeground;
    setAutoFillBackground(true);
    setWindowFlags(Qt::FramelessWindowHint);
}


bool MythPopupBox::focusNextPrevChild(bool next)
{
    // -=>TODO: Temp fix... should re-evalutate/re-code.

    QList<QWidget *> objList = qFindChildren<QWidget *>(this);

    QWidget *pCurr    = focusWidget();
    QWidget *pNew     = NULL;
    int      nCurrIdx = -1;
    int      nIdx;

    for (nIdx = 0; nIdx < objList.size(); ++nIdx )
    {
        if (objList[ nIdx ] == pCurr)
        {
            nCurrIdx = nIdx;
            break;
        }
    }

    if (nCurrIdx == -1)
        return false;

    nIdx = nCurrIdx;

    do
    {
        if (next)
        {
            ++nIdx;

            if (nIdx == objList.size())
                nIdx = 0;
        }
        else
        {
            --nIdx;

            if (nIdx < 0)
                nIdx = objList.size() -1;
        }

        pNew = objList[ nIdx ];

        if (pNew && !pNew->focusProxy() && pNew->isVisibleTo( this ) &&
            pNew->isEnabled() && (pNew->focusPolicy() != Qt::NoFocus))
        {
            pNew->setFocus();
            return true;
        }
    }
    while (nIdx != nCurrIdx);

    return false;

/*
    QFocusData *focusList = focusData();
    QObjectList *objList = queryList(NULL,NULL,false,true);

    QWidget *startingPoint = focusList->home();
    QWidget *candidate = NULL;

    QWidget *w = (next) ? focusList->prev() : focusList->next();

    int countdown = focusList->count();

    do
    {
        if (w && w != startingPoint && !w->focusProxy() &&
            w->isVisibleTo(this) && w->isEnabled() &&
            (objList->find((QObject *)w) != -1))
        {
            candidate = w;
        }

        w = (next) ? focusList->prev() : focusList->next();
    }
    while (w && !(candidate && w == startingPoint) && (countdown-- > 0));

    if (!candidate)
        return false;

    candidate->setFocus();
    return true;
*/
}

void MythPopupBox::addWidget(QWidget *widget, bool setAppearance)
{
    if (setAppearance == true)
    {
         widget->setPalette(palette());
         widget->setFont(font());
    }

    if (widget->metaObject()->className() == QString("QLabel"))
    {
        QPalette palette;
        palette.setColor(widget->foregroundRole(), popupForegroundColor);
        widget->setPalette(palette);
    }

    vbox->addWidget(widget);
}

QLabel *MythPopupBox::addLabel(QString caption, LabelSize size, bool wrap)
{
    QLabel *label = new QLabel(caption, this);
    switch (size)
    {
        case Large: label->setFont(defaultBigFont); break;
        case Medium: label->setFont(defaultMediumFont); break;
        case Small: label->setFont(defaultSmallFont); break;
    }

    label->setMaximumWidth((int)m_parent->width() / 2);
    if (wrap)
    {
        QChar::Direction text_dir = QChar::DirL;
        // Get a char from within the string to determine direction.
        if (caption.length())
            text_dir = caption[0].direction();
        Qt::Alignment align = (QChar::DirAL == text_dir) ?
            Qt::AlignRight : Qt::AlignLeft;
        label->setAlignment(align);
        label->setWordWrap(true);
    }

    label->setWordWrap(true);
    addWidget(label, false);
    return label;
}

QAbstractButton *MythPopupBox::addButton(QString caption, QObject *target,
                                 const char *slot)
{
    if (!target)
    {
        target = this;
        slot = SLOT(defaultButtonPressedHandler());
    }

    MythPushButton *button = new MythPushButton(caption, this, arrowAccel);
    m_parent->connect(button, SIGNAL(pressed()), target, slot);
    addWidget(button, false);
    return button;
}

void MythPopupBox::addLayout(QLayout *layout, int stretch)
{
    vbox->addLayout(layout, stretch);
}

void MythPopupBox::ShowPopup(QObject *target, const char *slot)
{
    ShowPopupAtXY(-1, -1, target, slot);
}

void MythPopupBox::ShowPopupAtXY(int destx, int desty,
                                 QObject *target, const char *slot)
{
    QList< QObject* > objlist = children();

    for (QList< QObject* >::Iterator it  = objlist.begin();
                                     it != objlist.end();
                                   ++it )
    {
        QObject *objs = *it;

        if (objs->isWidgetType())
        {
            QWidget *widget = (QWidget *)objs;
            widget->adjustSize();
        }
    }

    ensurePolished();

    int x = 0, y = 0, maxw = 0, poph = 0;

    for (QList< QObject* >::Iterator it  = objlist.begin();
                                     it != objlist.end();
                                   ++it )
    {
        QObject *objs = *it;

        if (objs->isWidgetType())
        {
            QString objname = objs->objectName();
            if (objname != "nopopsize")
            {
                // little extra padding for these guys
                if (objs->metaObject()->className() ==
                    QString("MythListBox"))
                {
                    poph += (int)(25 * hmult);
                }

                QWidget *widget = (QWidget *)objs;
                poph += widget->height();
                if (widget->width() > maxw)
                    maxw = widget->width();
            }
        }
    }

    poph += (int)(hpadding * hmult);
    setMinimumHeight(poph);

    maxw += (int)(wpadding * wmult);

    int width = (int)(800 * wmult);
    int height = (int)(600 * hmult);

    if (parentWidget())
    {
        width = parentWidget()->width();
        height = parentWidget()->height();
    }

    if (destx == -1)
        x = (int)(width / 2) - (int)(maxw / 2);
    else
        x = destx;

    if (desty == -1)
        y = (int)(height / 2) - (int)(poph / 2);
    else
        y = desty;

    if (poph + y > height)
        y = height - poph - (int)(8 * hmult);

    setFixedSize(maxw, poph);
    setGeometry(x, y, maxw, poph);

    if (target && slot)
        connect(this, SIGNAL(popupDone(int)), target, slot);

    Show();
}

void MythPopupBox::keyPressEvent(QKeyEvent *e)
{
    bool handled = false;
    QStringList actions;
    handled = GetMythMainWindow()->TranslateKeyPress("qt", e, actions);

    for (int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];

        if ((action == "ESCAPE") || (arrowAccel && action == "LEFT"))
        {
            reject();
            handled = true;
        }
    }

    if (!handled)
        MythDialog::keyPressEvent(e);
}

void MythPopupBox::AcceptItem(int i)
{
    MythDialog::AcceptItem(i);
    emit popupDone(rescode);
}

void MythPopupBox::accept(void)
{
    MythDialog::done(MythDialog::Accepted);
    emit popupDone(MythDialog::Accepted);
}

void MythPopupBox::reject(void)
{
    MythDialog::done(MythDialog::Rejected);
    emit popupDone(MythDialog::Rejected);
}

DialogCode MythPopupBox::ExecPopup(QObject *target, const char *slot)
{
    if (!target)
        ShowPopup(this, SLOT(done(int)));
    else
        ShowPopup(target, slot);

    return exec();
}

DialogCode MythPopupBox::ExecPopupAtXY(int destx, int desty,
                                       QObject *target, const char *slot)
{
    if (!target)
        ShowPopupAtXY(destx, desty, this, SLOT(done(int)));
    else
        ShowPopupAtXY(destx, desty, target, slot);

    return exec();
}

void MythPopupBox::defaultButtonPressedHandler(void)
{
    QList< QObject* > objlist = children();

    int i = 0;
    bool foundbutton = false;

    for (QList< QObject* >::Iterator it  = objlist.begin();
                                     it != objlist.end();
                                   ++it )
    {
        QObject *objs = *it;

        if (objs->isWidgetType())
        {
            QWidget *widget = (QWidget *)objs;
            if (widget->metaObject()->className() ==
                QString("MythPushButton"))
            {
                if (widget->hasFocus())
                {
                    foundbutton = true;
                    break;
                }
                i++;
            }
        }
    }
    if (foundbutton)
    {
        AcceptItem(i);
        return;
    }

    // this bit of code should always work but requires a cast
    i = 0;
    for (QList< QObject* >::Iterator it  = objlist.begin();
                                     it != objlist.end();
                                   ++it )
    {
        QObject *objs = *it;

        if (objs->isWidgetType())
        {
            QWidget *widget = (QWidget *)objs;
            if (widget->metaObject()->className() ==
                QString("MythPushButton"))
            {
                MythPushButton *button = dynamic_cast<MythPushButton*>(widget);
                if (button && button->isDown())
                {
                    foundbutton = true;
                    break;
                }
                i++;
            }
        }
    }
    if (foundbutton)
    {
        AcceptItem(i);
        return;
    }

    VERBOSE(VB_IMPORTANT, "MythPopupBox::defaultButtonPressedHandler(void)"
            "\n\t\t\tWe should never get here!");
    done(kDialogCodeRejected);
}

bool MythPopupBox::showOkPopup(
    MythMainWindow *parent,
    const QString  &title,
    const QString  &message,
    QString         button_msg)
{
    if (button_msg.isEmpty())
        button_msg = QObject::tr("OK");

    MythPopupBox *popup = new MythPopupBox(parent, title.toAscii().constData());

    popup->addLabel(message, MythPopupBox::Medium, true);
    QAbstractButton *okButton = popup->addButton(button_msg, popup, SLOT(accept()));
    okButton->setFocus();
    bool ret = (kDialogCodeAccepted == popup->ExecPopup());

    popup->hide();
    popup->deleteLater();

    return ret;
}

bool MythPopupBox::showOkCancelPopup(MythMainWindow *parent, QString title,
                                     QString message, bool focusOk)
{
    MythPopupBox *popup = new MythPopupBox(parent, title.toAscii().constData());

    popup->addLabel(message, Medium, true);
    QAbstractButton *okButton     = popup->addButton(tr("OK"),     popup, SLOT(accept()));
    QAbstractButton *cancelButton = popup->addButton(tr("Cancel"), popup, SLOT(reject()));

    if (focusOk)
        okButton->setFocus();
    else
        cancelButton->setFocus();

    bool ok = (Accepted == popup->ExecPopup());

    popup->hide();
    popup->deleteLater();

    return ok;
}

bool MythPopupBox::showGetTextPopup(MythMainWindow *parent, QString title,
                                    QString message, QString& text)
{
    MythPopupBox *popup = new MythPopupBox(parent, title.toAscii().constData());

    popup->addLabel(message, Medium, true);

    MythRemoteLineEdit *textEdit =
        new MythRemoteLineEdit(popup, "chooseEdit");

    textEdit->setText(text);
    popup->addWidget(textEdit);

    popup->addButton(tr("OK"),     popup, SLOT(accept()));
    popup->addButton(tr("Cancel"), popup, SLOT(reject()));

    textEdit->setFocus();

    bool ok = (Accepted == popup->ExecPopup());
    if (ok)
        text = textEdit->text();

    popup->hide();
    popup->deleteLater();

    return ok;
}

/**
 * \brief Like showGetTextPopup(), but doesn't echo the text entered.
 *
 * Uses MythLineEdit instead of MythRemoteLineEdit, so that PINs can be
 * entered without having to cycle through the number key "phone entry" mode.
 */
QString MythPopupBox::showPasswordPopup(MythMainWindow *parent,
                                        QString        title,
                                        QString        message)
{
    MythPopupBox *popup = new MythPopupBox(parent, title.toAscii().constData());

    popup->addLabel(message, Medium, true);

    MythLineEdit *entry = new MythLineEdit(popup, "passwordEntry");
    entry->setEchoMode(QLineEdit::Password);

    popup->addWidget(entry);

    popup->addButton(tr("OK"),    popup, SLOT(accept()));
    popup->addButton(tr("Cancel"),popup, SLOT(reject()));

    // Currently unused, because AllowVirtualKeyboard is set
    popup->m_parent->connect(entry, SIGNAL(returnPressed()),
                             popup, SLOT(accept()));
    entry->setFocus();

    QString password = QString();
    if (popup->ExecPopup() == Accepted)
        password = entry->text();

    popup->hide();
    popup->deleteLater();

    return password;
}

DialogCode MythPopupBox::Show2ButtonPopup(
    MythMainWindow *parent,
    const QString &title, const QString &message,
    const QString &button1msg, const QString &button2msg,
    DialogCode default_button)
{
    QStringList buttonmsgs;
    buttonmsgs += (button1msg.isEmpty()) ?
        QString("Button 1") : button1msg;
    buttonmsgs += (button2msg.isEmpty()) ?
        QString("Button 2") : button2msg;
    return ShowButtonPopup(
        parent, title, message, buttonmsgs, default_button);
}

DialogCode MythPopupBox::ShowButtonPopup(
    MythMainWindow    *parent,
    const QString     &title,
    const QString     &message,
    const QStringList &buttonmsgs,
    DialogCode         default_button)
{
    MythPopupBox *popup = new MythPopupBox(parent, title.toAscii().constData());

    popup->addLabel(message, Medium, true);
    popup->addLabel("");

    const int def = CalcItemIndex(default_button);
    for (int i = 0; i < buttonmsgs.size(); i++ )
    {
        QAbstractButton *but = popup->addButton(buttonmsgs[i]);
        if (def == i)
            but->setFocus();
    }

    DialogCode ret = popup->ExecPopup();

    popup->hide();
    popup->deleteLater();

    return ret;
}

MythProgressDialog::MythProgressDialog(
    const QString &message, int totalSteps,
    bool cancelButton, const QObject *target, const char *slot)
    : MythDialog(GetMythMainWindow(), "progress", false)
{
    setObjectName("MythProgressDialog");
    int screenwidth, screenheight;
    float wmult, hmult;

    GetMythUI()->GetScreenSettings(screenwidth, wmult, screenheight, hmult);

    setFont(GetMythUI()->GetMediumFont());

    GetMythUI()->ThemeWidget(this);

    int yoff = screenheight / 3;
    int xoff = screenwidth / 10;
    setGeometry(xoff, yoff, screenwidth - xoff * 2, yoff);
    setFixedSize(QSize(screenwidth - xoff * 2, yoff));

    msglabel = new QLabel();
    msglabel->setText(message);

    QVBoxLayout *vlayout = new QVBoxLayout();
    vlayout->addWidget(msglabel);

    progress = new QProgressBar();
    progress->setRange(0, totalSteps);

    QHBoxLayout *hlayout = new QHBoxLayout();
    hlayout->addWidget(progress);

    if (cancelButton && slot && target)
    {
        MythPushButton *button = new MythPushButton(
            QObject::tr("Cancel"), NULL);
        button->setFocus();
        hlayout->addWidget(button);
        connect(button, SIGNAL(pressed()), target, slot);
    }

    setTotalSteps(totalSteps);

    if (class LCD * lcddev = LCD::Get())
    {
        QList<LCDTextItem> textItems;

        textItems.append(LCDTextItem(1, ALIGN_CENTERED, message, "Generic",
                                     false));
        lcddev->switchToGeneric(textItems);
    }

    hlayout->setSpacing(5);

    vlayout->setMargin((int)(15 * wmult));
    vlayout->setStretchFactor(msglabel, 5);

    QWidget *hbox = new QWidget();
    hbox->setLayout(hlayout);
    vlayout->addWidget(hbox);

    QFrame *vbox = new QFrame(this);
    vbox->setObjectName(objectName() + "_vbox");
    vbox->setLineWidth(3);
    vbox->setMidLineWidth(3);
    vbox->setFrameShape(QFrame::Panel);
    vbox->setFrameShadow(QFrame::Raised);
    vbox->setLayout(vlayout);

    QVBoxLayout *lay = new QVBoxLayout();
    lay->addWidget(vbox);
    setLayout(lay);

    show();

    qApp->processEvents();
}

MythProgressDialog::~MythProgressDialog()
{
}

void MythProgressDialog::deleteLater(void)
{
    hide();
    MythDialog::deleteLater();
}

void MythProgressDialog::Close(void)
{
    accept();

    LCD *lcddev = LCD::Get();
    if (lcddev)
    {
        lcddev->switchToNothing();
        lcddev->switchToTime();
    }
}

void MythProgressDialog::setProgress(int curprogress)
{
    progress->setValue(curprogress);
    if (curprogress % steps == 0)
    {
        qApp->processEvents();
        if (LCD * lcddev = LCD::Get())
        {
            float fProgress = (float)curprogress / m_totalSteps;
            lcddev->setGenericProgress(fProgress);
        }
    }
}

void MythProgressDialog::setLabel(QString newlabel)
{
    msglabel->setText(newlabel);
}

void MythProgressDialog::keyPressEvent(QKeyEvent *e)
{
    bool handled = false;
    QStringList actions;
    handled = GetMythMainWindow()->TranslateKeyPress("qt", e, actions);

    for (int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];
        if (action == "ESCAPE")
            handled = true;
    }

    if (!handled)
        MythDialog::keyPressEvent(e);
}

void MythProgressDialog::setTotalSteps(int totalSteps)
{
    m_totalSteps = totalSteps;
    progress->setRange(0, totalSteps);
    steps = totalSteps / 1000;
    if (steps == 0)
        steps = 1;
}

MythBusyDialog::MythBusyDialog(const QString &title, bool cancelButton,
                               const QObject *target, const char *slot)
    : MythProgressDialog(title, 0,
                         cancelButton, target, slot),
                         timer(NULL)
{
    setObjectName("MythBusyDialog");
}

MythBusyDialog::~MythBusyDialog()
{
    Teardown();
}

void MythBusyDialog::deleteLater(void)
{
    Teardown();
    MythProgressDialog::deleteLater();
}

void MythBusyDialog::Teardown(void)
{
    if (timer)
    {
        timer->disconnect();
        timer = NULL;
    }
}

void MythBusyDialog::start(int interval)
{
    if (!timer)
        timer = new QTimer(this);

    connect(timer, SIGNAL(timeout()),
            this,  SLOT  (timeout()));

    timer->start(interval);
}

void MythBusyDialog::Close(void)
{
    if (timer)
    {
        timer->disconnect();
        timer = NULL;
    }

    MythProgressDialog::Close();
}

void MythBusyDialog::setProgress(void)
{
    progress->setValue(progress->value() + 10);
    qApp->processEvents();
    if (LCD *lcddev = LCD::Get())
        lcddev->setGenericBusy();
}

void MythBusyDialog::timeout(void)
{
    setProgress();
}

MythThemedDialog::MythThemedDialog(MythMainWindow *parent,
                                   const QString  &window_name,
                                   const QString  &theme_filename,
                                   const char     *name,
                                   bool            setsize)
                : MythDialog(parent, name, setsize)
{
    setNoErase();

    theme = NULL;

    if (!loadThemedWindow(window_name, theme_filename))
    {
        QString msg =
            QString(tr("Could not locate '%1' in theme '%2'."
                       "\n\nReturning to the previous menu."))
            .arg(window_name).arg(theme_filename);
        MythPopupBox::showOkPopup(GetMythMainWindow(),
                                  tr("Missing UI Element"), msg);
        reject();
        return;
    }
}

MythThemedDialog::MythThemedDialog(MythMainWindow *parent, const char* name,
                                   bool setsize)
                : MythDialog(parent, name, setsize)
{
    setNoErase();
    theme = NULL;
}

bool MythThemedDialog::loadThemedWindow(QString window_name,
                                        QString theme_filename)
{
    if (theme)
        delete theme;

    context = -1;
    my_containers.clear();
    widget_with_current_focus = NULL;

    redrawRect = QRect(0, 0, 0, 0);

    theme = new XMLParse();
    theme->SetWMult(wmult);
    theme->SetHMult(hmult);
    if (!theme->LoadTheme(xmldata, window_name, theme_filename))
    {
        return false;
    }

    loadWindow(xmldata);

    //
    //  Auto-connect signals we know about
    //

    //  Loop over containers
    QList<LayerSet*>::iterator an_it = my_containers.begin();
    for (; an_it != my_containers.end(); ++an_it)
    {
        LayerSet *looper = *an_it;
        //  Loop over UITypes within each container
        vector<UIType *> *all_ui_type_objects = looper->getAllTypes();
        vector<UIType *>::iterator i = all_ui_type_objects->begin();
        for (; i != all_ui_type_objects->end(); i++)
        {
            UIType *type = (*i);
            connect(type, SIGNAL(requestUpdate()), this,
                    SLOT(updateForeground()));
            connect(type, SIGNAL(requestUpdate(const QRect &)), this,
                    SLOT(updateForeground(const QRect &)));
            connect(type, SIGNAL(requestRegionUpdate(const QRect &)), this,
                    SLOT(updateForegroundRegion(const QRect &)));
        }
    }

    buildFocusList();

    updateBackground();
    initForeground();

    return true;
}

bool MythThemedDialog::buildFocusList()
{
    //
    //  Build a list of widgets that will take focus
    //

    focus_taking_widgets.clear();


    //  Loop over containers
    QList<LayerSet*>::iterator another_it = my_containers.begin();
    for (; another_it != my_containers.end(); ++another_it)
    {
        LayerSet *looper = *another_it;
        //  Loop over UITypes within each container
        vector<UIType *> *all_ui_type_objects = looper->getAllTypes();
        vector<UIType *>::iterator i = all_ui_type_objects->begin();
        for (; i != all_ui_type_objects->end(); i++)
        {
            UIType *type = (*i);
            if (type->canTakeFocus() && !type->isHidden() &&
                (context == -1 || type->GetContext() == -1 ||
                 context == type->GetContext()))
            {
                focus_taking_widgets.push_back(type);
            }
        }
    }

    return !focus_taking_widgets.empty();
}

MythThemedDialog::~MythThemedDialog()
{
    if (theme)
    {
        delete theme;
        theme = NULL;
    }
}

void MythThemedDialog::deleteLater(void)
{
    if (theme)
    {
        delete theme;
        theme = NULL;
    }
    MythDialog::deleteLater();
}

void MythThemedDialog::loadWindow(QDomElement &element)
{
    //
    //  Parse all the child elements in the theme
    //

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement e = child.toElement();
        if (!e.isNull())
        {
            if (e.tagName() == "font")
            {
                theme->parseFont(e);
            }
            else if (e.tagName() == "container")
            {
                parseContainer(e);
            }
            else if (e.tagName() == "popup")
            {
                parsePopup(e);
            }
            else
            {
                VERBOSE(VB_IMPORTANT,
                        QString("MythThemedDialog::loadWindow(): Do not "
                                "understand DOM Element: '%1'. Ignoring.")
                        .arg(e.tagName()));
            }
        }
    }
}

void MythThemedDialog::parseContainer(QDomElement &element)
{
    //
    //  Have the them object parse the containers
    //  but hold a pointer to each of them so
    //  that we can iterate over them later
    //

    QRect area;
    QString name;
    int a_context;
    theme->parseContainer(element, name, a_context, area);
    if (name.length() < 1)
    {
        VERBOSE(VB_IMPORTANT, "Failed to parse a container. Ignoring.");
        return;
    }

    LayerSet *container_reference = theme->GetSet(name);
    my_containers.append(container_reference);
}

void MythThemedDialog::parseFont(QDomElement &element)
{
    //
    //  this is just here so you can re-implement the virtual
    //  function and do something special if you like
    //

    theme->parseFont(element);
}

void MythThemedDialog::parsePopup(QDomElement &element)
{
    //
    //  theme doesn't know how to do this yet
    //
    element = element;
    VERBOSE(VB_IMPORTANT,
            "MythThemedDialog cannot parse popups yet - ignoring");
}

void MythThemedDialog::initForeground()
{
    my_foreground = my_background;
    updateForeground();
}

void MythThemedDialog::updateBackground()
{
    //
    //  Draw the background pixmap once
    //

    QPixmap bground(size());
    bground.fill(this, 0, 0);

    QPainter tmp(&bground);

    //
    //  Ask the container holding anything
    //  to do with the background to draw
    //  itself on a pixmap
    //

    LayerSet *container = theme->GetSet("background");

    //
    //  *IFF* there is a background, draw it
    //
    if (container)
    {
        container->Draw(&tmp, 0, context);
        tmp.end();
    }

    //
    //  Copy that pixmap to the permanent one
    //  and tell Qt about it
    //

    my_background = bground;
    QPalette palette;
    palette.setBrush(backgroundRole(), QBrush(my_background));
    setPalette(palette);
}

void MythThemedDialog::updateForeground()
{
    QRect r = this->geometry();
    updateForeground(r);
}

QString ZeroSizedRect = QString("MythThemedDialog - Something is requesting"
" a screen update of zero size. A widget probably has not done"
" calculateScreeArea(). Will redraw the whole screen (inefficient!).");

void MythThemedDialog::updateForeground(const QRect &r)
{
    QRect rect_to_update = r;
    if (r.width() == 0 || r.height() == 0)
    {
        VERBOSE(VB_IMPORTANT, ZeroSizedRect);
        rect_to_update = this->geometry();
    }

    redrawRect = redrawRect.unite(r);

    update(redrawRect);
}

void MythThemedDialog::ReallyUpdateForeground(const QRect &r)
{
    QRect rect_to_update = r;
    if (r.width() == 0 || r.height() == 0)
    {
        VERBOSE(VB_IMPORTANT, ZeroSizedRect);
        rect_to_update = this->geometry();
    }

    UpdateForegroundRect(rect_to_update);

    redrawRect = QRect(0, 0, 0, 0);
}

void MythThemedDialog::updateForegroundRegion(const QRect &r)
{
    // Note: DrawRegion is never actually called now. Instead
    // of controls (only UIListTreeType did it) implementing an
    // optimized paint, we rely on clipping regions.
    UpdateForegroundRect(r);

    update(r);
}

void MythThemedDialog::UpdateForegroundRect(const QRect &inv_rect)
{
    QPainter whole_dialog_painter(&my_foreground);

    // Updating the background portion isn't optional. The old
    // behavior left remnants during context transitions if
    // they happened outside active containers for the current
    // context.
    whole_dialog_painter.drawPixmap(inv_rect.topLeft(), my_background,
                                    inv_rect);

    QList<LayerSet*>::iterator an_it = my_containers.begin();
    for (; an_it != my_containers.end(); ++an_it)
    {
        LayerSet *looper = *an_it;
        QRect container_area = looper->GetAreaRect();

        //
        //  Only paint if the container's area is valid
        //  and it intersects with whatever Qt told us
        //  needed to be repainted
        //

        const QRect intersect = inv_rect.intersect(container_area);
        int looper_context = looper->GetContext();
        if (container_area.isValid() &&
            (looper_context == context || looper_context == -1) &&
            intersect.isValid() &&
            looper->GetName().toLower() != "background")
        {
            //
            //  Debugging
            //
            /*
            cout << "A container called \"" << looper->GetName()
                 << "\" said its area is "
                 << container_area.left()
                 << ","
                 << container_area.top()
                 << " to "
                 << container_area.left() + container_area.width()
                 << ","
                 << container_area.top() + container_area.height()
                 << endl;
            */

            //
            //  Loop over the draworder layers

            whole_dialog_painter.save();

            whole_dialog_painter.setClipRect(intersect);
            whole_dialog_painter.translate(container_area.left(),
                                           container_area.top());

            for (int i = 0; i <= looper->getLayers(); ++i)
            {
                looper->Draw(&whole_dialog_painter, i, context);
            }

            whole_dialog_painter.restore();
        }
    }
}

void MythThemedDialog::paintEvent(QPaintEvent *e)
{
    if (redrawRect.width() > 0 && redrawRect.height() > 0)
        ReallyUpdateForeground(redrawRect);

    { // Make sure QPainter is destructed before calling MythDialog's paint..
        QPainter p(this);
        p.drawPixmap(e->rect().topLeft(), my_foreground, e->rect());
    }
    MythDialog::paintEvent(e);
}

bool MythThemedDialog::assignFirstFocus()
{
    if (widget_with_current_focus)
    {
        widget_with_current_focus->looseFocus();
    }

    vector<UIType*>::iterator an_it = focus_taking_widgets.begin();
    for (; an_it != focus_taking_widgets.end(); ++an_it)
    {
        UIType *looper = *an_it;
        if (looper->canTakeFocus())
        {
            widget_with_current_focus = looper;
            widget_with_current_focus->takeFocus();
            return true;
        }
    }

    return false;
}

bool MythThemedDialog::nextPrevWidgetFocus(bool up_or_down)
{
    if (up_or_down)
    {
        bool reached_current = false;

        vector<UIType*>::iterator an_it = focus_taking_widgets.begin();
        for (; an_it != focus_taking_widgets.end(); ++an_it)
        {
            UIType *looper = *an_it;
            if (reached_current && looper->canTakeFocus())
            {
                widget_with_current_focus->looseFocus();
                widget_with_current_focus = looper;
                widget_with_current_focus->takeFocus();
                return true;
            }

            if (looper == widget_with_current_focus)
            {
                reached_current= true;
            }
        }

        if (assignFirstFocus())
        {
            return true;
        }
        return false;
    }
    else
    {
        bool reached_current = false;

        vector<UIType*>::reverse_iterator an_it = focus_taking_widgets.rbegin();
        for (; an_it != focus_taking_widgets.rend(); ++an_it)
        {
            UIType *looper = *an_it;

            if (reached_current && looper->canTakeFocus())
            {
                widget_with_current_focus->looseFocus();
                widget_with_current_focus = looper;
                widget_with_current_focus->takeFocus();
                return true;
            }

            if (looper == widget_with_current_focus)
            {
                reached_current= true;
            }
        }

        if (reached_current)
        {
            an_it = focus_taking_widgets.rbegin();
            for (; an_it != focus_taking_widgets.rend(); ++an_it)
            {
                UIType *looper = *an_it;

                if (looper->canTakeFocus())
                {
                    widget_with_current_focus->looseFocus();
                    widget_with_current_focus = looper;
                    widget_with_current_focus->takeFocus();
                    return true;
                }
            }
        }
        return false;
    }
    return false;
}

void MythThemedDialog::activateCurrent()
{
    if (widget_with_current_focus)
    {
        widget_with_current_focus->activate();
    }
    else
    {
        VERBOSE(VB_IMPORTANT, "MythThemedDialog::activateCurrent() - "
                              "there is no current widget!");
    }
}

namespace
{
    template <typename T>
    T *GetUIType(MythThemedDialog *dialog, const QString &name)
    {
        UIType *sf = dialog->getUIObject(name);
        if (sf)
        {
            T *ret = dynamic_cast<T *>(sf);
            if (ret)
                return ret;
        }
        return 0;
    }
};

UIType* MythThemedDialog::getUIObject(const QString &name)
{
    //
    //  Try and find the UIType target referenced
    //  by "name".
    //
    //  At some point, it would be nice to speed
    //  this up with a map across all instantiated
    //  UIType objects "owned" by this dialog
    //

    QList<LayerSet*>::iterator an_it = my_containers.begin();
    for (; an_it != my_containers.end(); ++an_it)
    {
        UIType *hunter = (*an_it)->GetType(name);
        if (hunter)
            return hunter;
    }

    return NULL;
}

UIType* MythThemedDialog::getCurrentFocusWidget()
{
    if (widget_with_current_focus)
    {
        return widget_with_current_focus;
    }
    return NULL;
}

void MythThemedDialog::setCurrentFocusWidget(UIType* widget)
{
    // make sure this widget is in the list of widgets that can take focus
    vector<UIType*>::iterator it =
        std::find(focus_taking_widgets.begin(),
                  focus_taking_widgets.end(), widget);
    if (it == focus_taking_widgets.end())
        return;

    if (widget_with_current_focus)
        widget_with_current_focus->looseFocus();

    widget_with_current_focus = widget;
    widget_with_current_focus->takeFocus();
}

UIManagedTreeListType* MythThemedDialog::getUIManagedTreeListType(
    const QString &name)
{
    return GetUIType<UIManagedTreeListType>(this, name);
}

UITextType* MythThemedDialog::getUITextType(const QString &name)
{
    return GetUIType<UITextType>(this, name);
}

UIRemoteEditType* MythThemedDialog::getUIRemoteEditType(const QString &name)
{
    return GetUIType<UIRemoteEditType>(this, name);
}

UIPushButtonType* MythThemedDialog::getUIPushButtonType(const QString &name)
{
    return GetUIType<UIPushButtonType>(this, name);
}

UITextButtonType* MythThemedDialog::getUITextButtonType(const QString &name)
{
    return GetUIType<UITextButtonType>(this, name);
}

UIRepeatedImageType* MythThemedDialog::getUIRepeatedImageType(
    const QString &name)
{
    return GetUIType<UIRepeatedImageType>(this, name);
}

UICheckBoxType* MythThemedDialog::getUICheckBoxType(const QString &name)
{
    return GetUIType<UICheckBoxType>(this, name);
}

UISelectorType* MythThemedDialog::getUISelectorType(const QString &name)
{
    return GetUIType<UISelectorType>(this, name);
}

UIBlackHoleType* MythThemedDialog::getUIBlackHoleType(const QString &name)
{
    return GetUIType<UIBlackHoleType>(this, name);
}

UIImageType* MythThemedDialog::getUIImageType(const QString &name)
{
    return GetUIType<UIImageType>(this, name);
}

UIStatusBarType* MythThemedDialog::getUIStatusBarType(const QString &name)
{
    return GetUIType<UIStatusBarType>(this, name);
}

UIListBtnType* MythThemedDialog::getUIListBtnType(const QString &name)
{
    return GetUIType<UIListBtnType>(this, name);
}

UIListTreeType* MythThemedDialog::getUIListTreeType(const QString &name)
{
    return GetUIType<UIListTreeType>(this, name);
}

UIKeyboardType *MythThemedDialog::getUIKeyboardType(const QString &name)
{
    return GetUIType<UIKeyboardType>(this, name);
}

UIImageGridType* MythThemedDialog::getUIImageGridType(const QString &name)
{
    return GetUIType<UIImageGridType>(this, name);
}

LayerSet *MythThemedDialog::getContainer(const QString &name)
{
    QList<LayerSet*>::iterator an_it = my_containers.begin();
    for (; an_it != my_containers.end(); ++an_it)
    {
        if ((*an_it)->GetName() == name)
            return *an_it;
    }

    return NULL;
}

fontProp* MythThemedDialog::getFont(const QString &name)
{
    fontProp* font = NULL;
    if (theme)
        font = theme->GetFont(name, true);

    return font;
}

/**
 * \class MythPasswordDialog
 * \brief Display a window which accepts a password and verifies it.
 *
 * Needs the password to be passed in as an argument, because it does the
 * checking locally, and sets a bool if the entered password matches it.
 */

/**
 * \param success Pointer to storage for the result
 * \param target  The password we are trying to match
 */
MythPasswordDialog::MythPasswordDialog(QString message,
                                       bool *success,
                                       QString target,
                                       MythMainWindow *parent,
                                       const char *name,
                                       bool)
                   :MythDialog(parent, name, false)
{
    int textWidth =  fontMetrics().width(message);
    int totalWidth = textWidth + 175;

    success_flag = success;
    target_text = target;

    GetMythUI()->GetScreenSettings(screenwidth, wmult, screenheight, hmult);
    this->setGeometry((screenwidth - 250 ) / 2,
                      (screenheight - 50 ) / 2,
                      totalWidth,50);
    QFrame *outside_border = new QFrame(this);
    outside_border->setObjectName(objectName() + "_outside_border");
    outside_border->setGeometry(0,0,totalWidth,50);
    outside_border->setFrameStyle(QFrame::Panel | QFrame::Raised );
    outside_border->setLineWidth(4);

    QLabel *message_label = new QLabel(message, this);
    outside_border->setObjectName(objectName() + "_message_label");
    message_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    message_label->setGeometry(15,10,textWidth,30);

    password_editor = new MythLineEdit(
        this, QString(objectName()+"_password_editor").toAscii().constData());
    password_editor->setEchoMode(QLineEdit::Password);
    password_editor->setGeometry(textWidth + 20,10,135,30);
    password_editor->setAllowVirtualKeyboard(false);
    connect(password_editor, SIGNAL(textChanged(const QString &)),
            this, SLOT(checkPassword(const QString &)));

    activateWindow();
    password_editor->setFocus();
}

void MythPasswordDialog::keyPressEvent(QKeyEvent *e)
{
    bool handled = false;
    QStringList actions;
    handled = GetMythMainWindow()->TranslateKeyPress("qt", e, actions);

    for (int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];
        if (action == "ESCAPE")
        {
            handled = true;
            MythDialog::keyPressEvent(e);
        }
    }
}


void MythPasswordDialog::checkPassword(const QString &the_text)
{
    if (the_text == target_text)
    {
        *success_flag = true;
        accept();
    }
    else
    {
        //  Oh to beep
    }
}

MythPasswordDialog::~MythPasswordDialog()
{
}

/*
---------------------------------------------------------------------
*/

MythSearchDialog::MythSearchDialog(MythMainWindow *parent, const char *name)
                 :MythPopupBox(parent, name)
{
    // create the widgets
    caption = addLabel(QString(""));

    editor = new MythRemoteLineEdit(this);
    connect(editor, SIGNAL(textChanged()), this, SLOT(searchTextChanged()));
    addWidget(editor);
    editor->setFocus();
    editor->setPopupPosition(VKQT_POSBOTTOMDIALOG);

    listbox = new MythListBox(this);
    connect(listbox, SIGNAL(accepted(int)), this, SLOT(AcceptItem(int)));
    addWidget(listbox);

    ok_button     = addButton(tr("OK"),     this, SLOT(accept()));
    cancel_button = addButton(tr("Cancel"), this, SLOT(reject()));
}

void MythSearchDialog::keyPressEvent(QKeyEvent *e)
{
    bool handled = false;
    QStringList actions;
    handled = GetMythMainWindow()->TranslateKeyPress("qt", e, actions);

    for (int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];
        if (action == "ESCAPE")
        {
            handled = true;
            reject();
        }
        if (action == "LEFT")
        {
            handled = true;
            focusNextPrevChild(false);
        }
        if (action == "RIGHT")
        {
            handled = true;
            focusNextPrevChild(true);
        }
        if (action == "SELECT")
        {
            handled = true;
            accept();
        }
    }

    if (!handled)
        MythPopupBox::keyPressEvent(e);
}

void MythSearchDialog::setCaption(QString text)
{
    if (caption)
        caption->setText(text);
}

void MythSearchDialog::setSearchText(QString text)
{
    if (editor)
    {
        editor->clear();
        editor->insertPlainText(text);
    }
}

void MythSearchDialog::searchTextChanged(void)
{
    if (listbox && editor)
    {
        listbox->setCurrentItem(editor->text(), false,  true);
        listbox->setTopRow(listbox->currentRow());
    }
}

QString MythSearchDialog::getResult(void)
{
    if (listbox)
        return listbox->currentText();

    // Don't return QString::null, might cause segfaults due to
    // code that doesn't check the return value...
    return "";
}

void MythSearchDialog::setItems(QStringList items)
{
    if (listbox)
    {
        listbox->insertStringList(items);
        searchTextChanged();
    }
}

MythSearchDialog::~MythSearchDialog()
{
    Teardown();
}

void MythSearchDialog::deleteLater(void)
{
    Teardown();
    MythPopupBox::deleteLater();
}

void MythSearchDialog::Teardown(void)
{
    caption       = NULL; // deleted by Qt

    if (editor)
    {
        editor->disconnect();
        editor    = NULL; // deleted by Qt
    }

    if (listbox)
    {
        listbox->disconnect();
        listbox   = NULL; // deleted by Qt
    }

    ok_button     = NULL; // deleted by Qt
    cancel_button = NULL; // deleted by Qt
}
