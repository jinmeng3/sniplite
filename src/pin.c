/* Pin windows: drag to move, wheel to zoom, Ctrl+wheel for opacity,
 * double-click or Esc to close, right-click for a menu. */
#include "pin.h"
#include "image.h"
#include "lang.h"
#include "ocr.h"
#include "config.h"
#include "settings.h"
#include <dwmapi.h>
#include <math.h>
#include <windowsx.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#define TIMER_LABEL 1
enum { CM_COPY = 1, CM_SAVE, CM_OCR, CM_RESET, CM_TOP_ALL, CM_TOP, CM_CLOSE, CM_CLOSE_ALL, CM_SETTINGS, CM_RACK, CM_STORE };
enum { RACK_PAD = 16, RACK_STEP = 104, RACK_CARD = 96, RACK_Y = 48, RACK_FOOT = 24 };

typedef struct Pin {
    HWND hwnd;
    Img img;
    float zoom;
    int alpha; /* percent, 10..100 */
    bool top;  /* this pin above other windows, whatever the setting for all of them says */
    bool drag;
    POINT drag_pt, win_pt;
    bool stored;
    RECT restore_rect; /* bounds before dragging or storing; zoom and opacity stay intact */
    Img thumb;
    bool show_label;
    WCHAR label[32];
    struct Pin *next;
} Pin;

static struct {
    HINSTANCE inst;
    HWND daemon;
    Pin *list;
    int count;
    bool top_all; /* the setting: every pin above other windows */
    HWND rack, rack_down;
    int rack_first;
    bool rack_open;
    int rack_hover, rack_pressed, rack_dpi;
    bool rack_tracking;
    Img rack_img;
    HFONT rack_font, rack_small;
    bool rack_reorder;
    POINT rack_down_pt;
    int rack_selected;
} P;

/* Onto the screen with the picture already drawn. The window has a frame (for the shadow;
 * WM_NCCALCSIZE leaves none of it in sight), and Windows first shows such a window as that
 * frame, white with its title on top, until the program has painted. Cloaked, it is shown to
 * nobody while it paints. */
static void pin_reveal(HWND hw)
{
    BOOL on = TRUE, off = FALSE;
    DwmSetWindowAttribute(hw, DWMWA_CLOAK, &on, sizeof on);
    ShowWindow(hw, SW_SHOWNOACTIVATE);
    UpdateWindow(hw);
    DwmSetWindowAttribute(hw, DWMWA_CLOAK, &off, sizeof off);
}

/* A pin is above other windows when all pins are, or when it was told to be itself. */
static void pin_layer(Pin *p)
{
    SetWindowPos(p->hwnd, P.top_all || p->top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void pin_top_all(bool on)
{
    P.top_all = on;
    for (Pin *p = P.list; p; p = p->next)
        pin_layer(p);
}

static Pin *pin_of(HWND h) { return (Pin *)GetWindowLongPtrW(h, GWLP_USERDATA); }

static int rack_px(int n)
{
    return MulDiv(n, P.rack ? (int)GetDpiForWindow(P.rack) : 96, 96);
}

static int rack_count(void)
{
    int n = 0;
    for (Pin *p = P.list; p; p = p->next)
        n += p->stored;
    return n;
}

static int rack_slots(void)
{
    RECT r;
    GetClientRect(P.rack, &r);
    return MAXI(1, (r.right - rack_px(2 * RACK_PAD)) / rack_px(RACK_STEP));
}

static void rack_refresh(void)
{
    if (!P.rack)
        return;
    P.rack_first = CLAMPI(P.rack_first, 0, MAXI(0, rack_count() - rack_slots()));
    InvalidateRect(P.rack, NULL, FALSE);
}

static RECT rack_button(int i)
{
    RECT r;
    GetClientRect(P.rack, &r);
    int x = r.right - rack_px(10 + (3 - i) * 32);
    RECT b = {x, rack_px(8), x + rack_px(28), rack_px(36)};
    return b;
}

/* 0..2: header buttons; 3 onward: thumbnail slots; -1: background. */
static int rack_target(POINT pt)
{
    RECT r;
    GetClientRect(P.rack, &r);
    if (!rect_has(&r, pt.x, pt.y))
        return -1;
    for (int i = 0; i < 3; i++) {
        RECT b = rack_button(i);
        if (rect_has(&b, pt.x, pt.y))
            return i;
    }
    if (pt.x >= rack_px(RACK_PAD) && pt.y >= rack_px(RACK_Y) && pt.y < r.bottom - rack_px(RACK_FOOT)) {
        int slot = (pt.x - rack_px(RACK_PAD)) / rack_px(RACK_STEP);
        int offset = (pt.x - rack_px(RACK_PAD)) % rack_px(RACK_STEP);
        if (slot < rack_slots() && offset < rack_px(RACK_CARD))
            return slot + 3;
    }
    return -1;
}

static Pin *rack_hit(POINT pt)
{
    int target = rack_target(pt);
    if (target < 3)
        return NULL;
    int at = P.rack_first + target - 3;
    for (Pin *p = P.list; p; p = p->next)
        if (p->stored && at-- == 0)
            return p;
    return NULL;

}
static void rack_restore(Pin *p);
static Pin *rack_at(int at)
{
    if (at < 0)
        return NULL;
    for (Pin *p = P.list; p; p = p->next)
        if (p->stored && at-- == 0)
            return p;
    return NULL;
}

static int rack_index(Pin *pin)
{
    int at = 0;
    for (Pin *p = P.list; p; p = p->next) {
        if (p == pin)
            return p->stored ? at : -1;
        at += p->stored;
    }
    return -1;
}

static void rack_move(Pin *from, Pin *to)
{
    if (!from || !to || from == to || !from->stored || !to->stored)
        return;
    Pin **src = &P.list;
    while (*src && *src != from)
        src = &(*src)->next;
    if (!*src)
        return;
    *src = from->next;
    Pin **dst = &P.list;
    while (*dst && *dst != to)
        dst = &(*dst)->next;
    from->next = to;
    *dst = from;
}

static void rack_context(Pin *p, POINT at)
{
    if (!p)
        return;
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    AppendMenuW(menu, MF_STRING, 1, T(RACK_RESTORE));
    AppendMenuW(menu, MF_STRING, 2, T(RACK_CLOSE_ONE));
    HWND hwnd = p->hwnd;
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x, at.y, 0, P.rack, NULL);
    DestroyMenu(menu);
    if (!IsWindow(hwnd) || pin_of(hwnd) != p)
        return;
    if (cmd == 1)
        rack_restore(p);
    else if (cmd == 2)
        DestroyWindow(hwnd);
}

static void rack_restore(Pin *p)
{
    p->stored = false;
    img_destroy(&p->thumb);
    RECT r = p->restore_rect;
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = CLAMPI(r.left, mi.rcWork.left, MAXI(mi.rcWork.left, mi.rcWork.right - w));
    int y = CLAMPI(r.top, mi.rcWork.top, MAXI(mi.rcWork.top, mi.rcWork.bottom - h));
    SetWindowPos(p->hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    pin_reveal(p->hwnd);
    rack_refresh();
}

static void rack_dismiss(void)
{
    P.rack_open = false;
    ShowWindow(P.rack, SW_HIDE);
    for (Pin *p = P.list; p; p = p->next)
        if (p->stored)
            rack_restore(p);
}

static void rack_box(HDC dc, RECT r, COLORREF fill, COLORREF edge, int radius)
{
    HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, fill);
    SetDCPenColor(dc, edge);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rack_px(radius), rack_px(radius));
    SelectObject(dc, pen);
    SelectObject(dc, brush);
}

static void rack_paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC target = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    if (P.rack_img.w != rc.right || P.rack_img.h != rc.bottom) {
        img_destroy(&P.rack_img);
        if (!img_create(&P.rack_img, rc.right, rc.bottom)) {
            FillRect(target, &ps.rcPaint, GetSysColorBrush(COLOR_WINDOW));
            EndPaint(h, &ps);
            return;
        }
    }
    int dpi = (int)GetDpiForWindow(h);
    if (dpi != P.rack_dpi) {
        DeleteObject(P.rack_font);
        DeleteObject(P.rack_small);
        P.rack_font = CreateFontW(-rack_px(13), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, T(FONT));
        P.rack_small = CreateFontW(-rack_px(10), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, T(FONT));
        P.rack_dpi = dpi;
    }
    HDC dc = P.rack_img.dc;
    HGDIOBJ font = SelectObject(dc, P.rack_font);
    SetBkMode(dc, TRANSPARENT);
    SetDCBrushColor(dc, RGB(247, 249, 253));
    FillRect(dc, &rc, GetStockObject(DC_BRUSH));
    rack_box(dc, rc, RGB(247, 249, 253), RGB(215, 224, 237), 18);
    RECT emblem = {rack_px(16), rack_px(10), rack_px(44), rack_px(38)};
    rack_box(dc, emblem, RGB(229, 239, 255), RGB(229, 239, 255), 10);
    HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, RGB(41, 111, 221));
    POINT hanger[] = {{rack_px(28), rack_px(19)}, {rack_px(28), rack_px(17)}, {rack_px(32), rack_px(17)},
                      {rack_px(32), rack_px(21)}, {rack_px(22), rack_px(29)}, {rack_px(38), rack_px(29)},
                      {rack_px(30), rack_px(23)}};
    Polyline(dc, hanger, COUNTOF(hanger));
    SelectObject(dc, pen);
    RECT title = {rack_px(54), rack_px(8), rc.right - rack_px(116), rack_px(40)};
    WCHAR text[160];
    int count = rack_count(), slots = rack_slots();
    wsprintfW(text, L"%s  \x00B7  %d", T(RACK_TITLE), count);
    SetTextColor(dc, RGB(36, 49, 71));
    DrawTextW(dc, text, -1, &title, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    for (int i = 0; i < 3; i++) {
        RECT b = rack_button(i);
        bool enabled = i == 2 || (i == 0 ? P.rack_first > 0 : P.rack_first + slots < count);
        bool hot = P.rack_hover == i && enabled, pressed = hot && P.rack_pressed == i;
        COLORREF bg = i == 2 && hot ? RGB(255, 233, 234) : pressed ? RGB(217, 229, 248) : hot ? RGB(231, 238, 249) : RGB(247, 249, 253);
        rack_box(dc, b, bg, bg, 10);
        HGDIOBJ old = SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, !enabled ? RGB(188, 199, 214) : i == 2 && hot ? RGB(205, 61, 76) : RGB(91, 110, 138));
        int x = (b.left + b.right) / 2, y = (b.top + b.bottom) / 2, d = rack_px(4);
        if (i == 2) {
            MoveToEx(dc, x - d, y - d, NULL); LineTo(dc, x + d + 1, y + d + 1);
            MoveToEx(dc, x + d, y - d, NULL); LineTo(dc, x - d - 1, y + d + 1);
        } else {
            int dir = i == 0 ? -1 : 1;
            POINT arrow[] = {{x - dir * d / 2, y - d}, {x + dir * d / 2, y}, {x - dir * d / 2, y + d}};
            Polyline(dc, arrow, COUNTOF(arrow));
        }
        SelectObject(dc, old);
    }
    SelectObject(dc, P.rack_small);
    int index = 0, slot = 0;
    for (Pin *p = P.list; p && slot < slots; p = p->next) {
        if (!p->stored || index++ < P.rack_first)
            continue;
        int x = rack_px(RACK_PAD + slot * RACK_STEP), y = rack_px(RACK_Y);
        bool hot = P.rack_hover == slot + 3, pressed = hot && P.rack_pressed == slot + 3;
        RECT card = {x, y, x + rack_px(RACK_CARD), rc.bottom - rack_px(RACK_FOOT)};
        bool selected = rack_index(p) == P.rack_selected;
        COLORREF bg = pressed ? RGB(222, 234, 255) : hot ? RGB(236, 243, 255) : RGB(255, 255, 255);
        rack_box(dc, card, bg, selected ? RGB(41, 111, 221) : hot ? RGB(91, 151, 240) : RGB(224, 231, 241), selected ? 14 : 12);
        float fit = fminf((float)rack_px(80) / p->thumb.w, (float)rack_px(56) / p->thumb.h);
        int w = MAXI(1, (int)lroundf(p->thumb.w * fit)), height = MAXI(1, (int)lroundf(p->thumb.h * fit));
        int dx = x + (rack_px(RACK_CARD) - w) / 2, dy = y + rack_px(7) + (rack_px(56) - height) / 2;
        if (w == p->thumb.w && height == p->thumb.h)
            BitBlt(dc, dx, dy, w, height, p->thumb.dc, 0, 0, SRCCOPY);
        else
            StretchBlt(dc, dx, dy, w, height, p->thumb.dc, 0, 0, p->thumb.w, p->thumb.h, SRCCOPY);
        RECT label = {x + rack_px(4), y + rack_px(65), card.right - rack_px(4), card.bottom - rack_px(3)};
        wsprintfW(text, L"%d \x00D7 %d", p->img.w, p->img.h);
        SetTextColor(dc, hot ? RGB(41, 111, 221) : RGB(121, 137, 160));
        DrawTextW(dc, text, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        slot++;
    }
    if (!count) {
        RECT hint = {rack_px(16), rack_px(RACK_Y), rc.right - rack_px(16), rc.bottom - rack_px(RACK_FOOT)};
        rack_box(dc, hint, RGB(238, 243, 251), RGB(220, 230, 244), 12);
        SetTextColor(dc, RGB(105, 126, 157));
        DrawTextW(dc, T(RACK_HINT), -1, &hint, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    RECT footer = {rack_px(18), rc.bottom - rack_px(22), rc.right - rack_px(104), rc.bottom - rack_px(2)};
    SetTextColor(dc, RGB(128, 144, 166));
    DrawTextW(dc, T(RACK_HELP), -1, &footer, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (count) {
        footer.left = rc.right - rack_px(90);
        footer.right = rc.right - rack_px(18);
        wsprintfW(text, L"%d\x2013%d / %d", P.rack_first + 1, MINI(P.rack_first + slots, count), count);
        DrawTextW(dc, text, -1, &footer, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
    SelectObject(dc, font);
    BitBlt(target, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
           dc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK rack_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_SHOWWINDOW:
        if (!wp) {
            P.rack_hover = P.rack_pressed = -1;
            P.rack_tracking = false;
            if (GetCapture() == h)
                ReleaseCapture();
        }
        break;
    case WM_PAINT: rack_paint(h); return 0;
    case WM_NCCALCSIZE:
        if (wp)
            return 0;
        break;
    case WM_NCACTIVATE: return DefWindowProcW(h, m, wp, -1);
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int hover = rack_target(pt);
        if (P.rack_down && (wp & MK_LBUTTON) && !P.rack_reorder) {
            int dx = pt.x - P.rack_down_pt.x, dy = pt.y - P.rack_down_pt.y;
            if (dx * dx + dy * dy >= rack_px(16) * rack_px(16))
                P.rack_reorder = true;
        }
        if (P.rack_reorder) {
            hover = rack_target(pt);
            P.rack_hover = hover;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        if (hover != P.rack_hover) {
            P.rack_hover = hover;
            InvalidateRect(h, NULL, FALSE);
        }
        if (!P.rack_tracking) {
            TRACKMOUSEEVENT track = {sizeof track, TME_LEAVE, h, 0};
            P.rack_tracking = TrackMouseEvent(&track);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        P.rack_tracking = false;
        P.rack_hover = -1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            SetCursor(LoadCursorW(NULL, rack_hit(pt) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &pt);
        return pt.y < rack_px(44) && rack_target(pt) < 0 ? HTCAPTION : HTCLIENT;
    }
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        Pin *p = rack_hit(pt);
        P.rack_down = p ? p->hwnd : NULL;
        P.rack_down_pt = pt;
        P.rack_reorder = false;
        P.rack_pressed = rack_target(pt);
        P.rack_selected = rack_index(p);
        SetFocus(h);
        InvalidateRect(h, NULL, FALSE);
        SetCapture(h);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        HWND down = P.rack_down;
        int pressed = P.rack_pressed;
        bool reorder = P.rack_reorder;
        P.rack_down = NULL;
        P.rack_pressed = -1;
        P.rack_reorder = false;
        ReleaseCapture();
        RECT r;
        GetClientRect(h, &r);
        if (!rect_has(&r, pt.x, pt.y))
            return 0;
        int button = rack_target(pt);
        if (button >= 0 && button < 3 && button == pressed) {
            if (button == 2)
                rack_dismiss();
            else
                P.rack_first += button == 0 ? -rack_slots() : rack_slots();
        } else if (pressed >= 3 && button >= 3) {
            Pin *from = pin_of(down), *to = rack_hit(pt);
            if (reorder && from && to && from != to) {
                rack_move(from, to);
                P.rack_selected = rack_index(from);
                P.rack_first = P.rack_selected;
            } else if (!reorder && from && from == to) {
                rack_restore(from);
                SetForegroundWindow(from->hwnd);
            }
        }
        rack_refresh();
        return 0;
    }
    case WM_CAPTURECHANGED:
        P.rack_down = NULL;
        P.rack_pressed = -1;
        P.rack_reorder = false;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
        P.rack_first += GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
        rack_refresh();
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        rack_refresh();
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        Pin *p = rack_hit(pt);
        ClientToScreen(h, &pt);
        rack_context(p, pt);
        rack_refresh();
        return 0;
    }
    case WM_SETFOCUS:
        if (P.rack_selected < 0 && rack_count())
            P.rack_selected = P.rack_first;
        rack_refresh();
        return 0;
    case WM_KEYDOWN: {
        int count = rack_count();
        if (wp == VK_ESCAPE) {
            rack_dismiss();
        } else if (count && (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_HOME || wp == VK_END)) {
            if (P.rack_selected < 0) P.rack_selected = P.rack_first;
            if (wp == VK_LEFT) P.rack_selected = MAXI(0, P.rack_selected - 1);
            if (wp == VK_RIGHT) P.rack_selected = MINI(count - 1, P.rack_selected + 1);
            if (wp == VK_HOME) P.rack_selected = 0;
            if (wp == VK_END) P.rack_selected = count - 1;
            P.rack_first = CLAMPI(P.rack_selected, 0, MAXI(0, count - rack_slots()));
            rack_refresh();
        } else if ((wp == VK_RETURN || wp == VK_DELETE) && P.rack_selected >= 0) {
            Pin *p = rack_at(P.rack_selected);
            if (p && wp == VK_RETURN)
                rack_restore(p);
            else if (p) {
                DestroyWindow(p->hwnd);
                P.rack_selected = MINI(P.rack_selected, rack_count() - 1);
                rack_refresh();
            }
        }
        return 0;
    }
    case WM_CLOSE: rack_dismiss(); return 0;
    case WM_NCDESTROY:
        P.rack = P.rack_down = NULL;
        img_destroy(&P.rack_img);
        DeleteObject(P.rack_font);
        DeleteObject(P.rack_small);
        P.rack_font = P.rack_small = NULL;
        P.rack_dpi = 0;
        P.rack_hover = P.rack_pressed = -1;
        P.rack_tracking = false;
        P.rack_open = false;
        P.rack_first = 0;
        break;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static bool rack_show(HWND pin)
{
    if (!P.rack) {
        RECT r;
        GetWindowRect(pin, &r);
        MONITORINFO mi = {sizeof mi};
        GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
        POINT at = {r.left, r.top};
        float s = dpi_scale_at(at);
        int w = MINI((int)lroundf(552.f * s), mi.rcWork.right - mi.rcWork.left);
        int h = (int)lroundf(156.f * s);
        P.rack_hover = P.rack_pressed = P.rack_selected = -1;
        P.rack = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"sniplite.rack", T(RACK_TITLE), WS_POPUP | WS_CAPTION,
                                mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2,
                                mi.rcWork.top + (int)lroundf(12.f * s), w, h, P.daemon, NULL, P.inst, NULL);
        if (!P.rack)
            return false;
        MARGINS margins = {0, 0, 0, 1};
        DwmExtendFrameIntoClientArea(P.rack, &margins);
        DWORD corner = 2; /* DWMWCP_ROUND; ignored on Windows 10 */
        DwmSetWindowAttribute(P.rack, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof corner);
        SetWindowPos(P.rack, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    P.rack_open = true;
    ShowWindow(P.rack, SW_SHOWNOACTIVATE);
    SetWindowPos(P.rack, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    rack_refresh();
    return true;
}

static void rack_store(Pin *p, RECT restore)
{
    if (p->stored || !rack_show(p->hwnd))
        return;
    float fit = fminf((float)rack_px(80) / p->img.w, (float)rack_px(56) / p->img.h);
    int w = MAXI(1, (int)lroundf(p->img.w * fit)), h = MAXI(1, (int)lroundf(p->img.h * fit));
    if (!img_create(&p->thumb, w, h))
        return;
    SetStretchBltMode(p->thumb.dc, HALFTONE);
    SetBrushOrgEx(p->thumb.dc, 0, 0, NULL);
    if (!StretchBlt(p->thumb.dc, 0, 0, w, h, p->img.dc, 0, 0, p->img.w, p->img.h, SRCCOPY)) {
        img_destroy(&p->thumb);
        return;
    }
    p->restore_rect = restore;
    p->stored = true;
    p->show_label = false;
    KillTimer(p->hwnd, TIMER_LABEL);
    ShowWindow(p->hwnd, SW_HIDE);
    P.rack_first = 0;
    int at = 0;
    for (Pin *q = P.list; q != p; q = q->next)
        at += q->stored;
    P.rack_first = at / rack_slots() * rack_slots();
    rack_refresh();
}

static void flash_label(Pin *p, const WCHAR *text)
{
    lstrcpynW(p->label, text, COUNTOF(p->label));
    p->show_label = true;
    SetTimer(p->hwnd, TIMER_LABEL, 900, NULL);
    InvalidateRect(p->hwnd, NULL, FALSE);
}

static void set_zoom(Pin *p, float z, POINT anchor)
{
    float maxz = fminf(16.f, 16000.f / (float)MAXI(p->img.w, p->img.h));
    z = fmaxf(0.05f, fminf(z, maxz));
    RECT wr;
    GetWindowRect(p->hwnd, &wr);
    float fx = (float)(anchor.x - wr.left) / (float)MAXI(1, wr.right - wr.left);
    float fy = (float)(anchor.y - wr.top) / (float)MAXI(1, wr.bottom - wr.top);
    int nw = MAXI(8, (int)lroundf((float)p->img.w * z)), nh = MAXI(8, (int)lroundf((float)p->img.h * z));
    int nx = anchor.x - (int)lroundf(fx * (float)nw), ny = anchor.y - (int)lroundf(fy * (float)nh);
    p->zoom = z;
    SetWindowPos(p->hwnd, NULL, nx, ny, nw, nh, SWP_NOZORDER | SWP_NOACTIVATE);
    WCHAR t[16];
    wsprintfW(t, L"%d%%", (int)lroundf(z * 100.f));
    flash_label(p, t);
}

static void set_alpha(Pin *p, int pct)
{
    p->alpha = CLAMPI(pct, 10, 100);
    LONG ex = GetWindowLongW(p->hwnd, GWL_EXSTYLE);
    if (p->alpha < 100) {
        if (!(ex & WS_EX_LAYERED))
            SetWindowLongW(p->hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(p->hwnd, 0, (BYTE)(p->alpha * 255 / 100), LWA_ALPHA);
    } else if (ex & WS_EX_LAYERED) {
        /* fully opaque again: drop the layered style, it costs composition work */
        SetWindowLongW(p->hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        RedrawWindow(p->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
    }
    WCHAR t[24];
    wsprintfW(t, T(PIN_OPACITY), p->alpha);
    flash_label(p, t);
}

static void pin_copy(Pin *p)
{
    clip_put_image(P.daemon, p->img.px, p->img.w, p->img.w, p->img.h);
    flash_label(p, T(PIN_COPIED));
}

static void pin_save(Pin *p)
{
    /* The dialog runs a nested message loop in which this pin may be closed,
     * so save from a private copy and re-validate the window afterwards. */
    Img copy;
    if (!img_create(&copy, p->img.w, p->img.h))
        return;
    memcpy(copy.px, p->img.px, (size_t)p->img.w * p->img.h * 4);
    HWND h = p->hwnd;
    WCHAR path[MAX_PATH * 2];
    if (save_dialog(path, COUNTOF(path))) {
        bool ok = image_save(path, copy.px, copy.w, copy.w, copy.h);
        if (IsWindow(h) && pin_of(h) == p)
            flash_label(p, ok ? T(PIN_SAVED) : T(PIN_SAVE_FAILED));
    }
    img_destroy(&copy);
}

static void pin_menu(Pin *p, POINT at)
{
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CM_COPY, T(PM_COPY));
    AppendMenuW(m, MF_STRING, CM_SAVE, T(PM_SAVE));
    if (ocr_there)
        AppendMenuW(m, MF_STRING, CM_OCR, T(PM_OCR));
    AppendMenuW(m, MF_STRING | (p->zoom == 1.f && p->alpha == 100 ? MF_GRAYED : 0), CM_RESET, T(PM_RESET));
    AppendMenuW(m, MF_STRING | (P.top_all ? MF_CHECKED : 0), CM_TOP_ALL, T(PM_TOP_ALL));
    AppendMenuW(m, MF_STRING | (p->top ? MF_CHECKED : 0), CM_TOP, T(PM_TOP));
    AppendMenuW(m, MF_STRING, CM_STORE, T(PM_STORE));
    AppendMenuW(m, MF_STRING, CM_RACK, T(RACK_TITLE));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, CM_CLOSE, T(PM_CLOSE));
    AppendMenuW(m, MF_STRING, CM_CLOSE_ALL, T(M_CLOSE_PINS));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, CM_SETTINGS, T(M_SETTINGS)); /* a way in that does not need the tray icon */
    HWND h = p->hwnd;
    SetForegroundWindow(h);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, at.x, at.y, 0, h, NULL);
    DestroyMenu(m);
    PostMessageW(h, WM_NULL, 0, 0);
    if (!IsWindow(h))
        return;
    switch (cmd) {
    case CM_COPY: pin_copy(p); break;
    case CM_SAVE: pin_save(p); break;
    case CM_OCR: { /* the text of the picture as it was pinned, to the clipboard */
        RECT wr;
        GetWindowRect(h, &wr);
        ocr_picture(p->img.px, p->img.w, p->img.h, wr, NULL);
        break;
    }
    case CM_RESET: {
        RECT wr;
        GetWindowRect(h, &wr);
        POINT tl = {wr.left, wr.top};
        set_alpha(p, 100);
        set_zoom(p, 1.f, tl);
        break;
    }
    case CM_TOP_ALL: /* the setting of the preferences, from here */
        pin_top_all(!P.top_all);
        config_set_pin_top(P.top_all);
        break;
    case CM_TOP:
        p->top = !p->top;
        pin_layer(p);
        break;
    case CM_CLOSE: DestroyWindow(h); break;
    case CM_CLOSE_ALL: pin_close_all(); break;
    case CM_SETTINGS: settings_open(NULL); break;
    case CM_RACK: rack_show(h); break;
    case CM_STORE: {
        RECT r;
        GetWindowRect(h, &r);
        rack_store(p, r);
        break;
    }
    }
}

static void paint(Pin *p)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(p->hwnd, &ps);
    RECT rc;
    GetClientRect(p->hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    HDC mem = NULL;
    HBITMAP bmp = NULL;
    HGDIOBJ old = NULL;
    int saved = 0;
    RECT lr = {0};
    if (p->show_label && w > 0 && h > 0) {
        float s = (float)GetDpiForWindow(p->hwnd) / 96.f;
        HFONT f = CreateFontW(-(int)lroundf(12.f * s), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, T(FONT));
        mem = CreateCompatibleDC(dc);
        if (mem) {
            HGDIOBJ of = SelectObject(mem, f);
            SIZE sz = {0};
            GetTextExtentPoint32W(mem, p->label, lstrlenW(p->label), &sz);
            int pad = (int)lroundf(6.f * s);
            lr.right = sz.cx + pad * 2;
            lr.bottom = sz.cy + pad;
            /* The label is opaque: buffer only its pixels, not the entire scaled image. */
            bmp = CreateCompatibleBitmap(dc, MINI(w, lr.right), MINI(h, lr.bottom));
            if (bmp) {
                old = SelectObject(mem, bmp);
                HBRUSH br = CreateSolidBrush(RGB(32, 32, 32));
                FillRect(mem, &lr, br);
                DeleteObject(br);
                SetBkMode(mem, TRANSPARENT);
                SetTextColor(mem, RGB(255, 255, 255));
                DrawTextW(mem, p->label, -1, &lr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                saved = SaveDC(dc);
                if (saved)
                    ExcludeClipRect(dc, 0, 0, lr.right, lr.bottom);
            }
            SelectObject(mem, of);
        }
        DeleteObject(f);
    }
    if (w == p->img.w && h == p->img.h) {
        BitBlt(dc, 0, 0, w, h, p->img.dc, 0, 0, SRCCOPY);
    } else {
        SetStretchBltMode(dc, w < p->img.w ? HALFTONE : COLORONCOLOR);
        SetBrushOrgEx(dc, 0, 0, NULL);
        StretchBlt(dc, 0, 0, w, h, p->img.dc, 0, 0, p->img.w, p->img.h, SRCCOPY);
    }
    /* an outline, so a pin stands out from what is behind it: the accent color on the pin in use */
    HBRUSH edge = CreateSolidBrush(GetForegroundWindow() == p->hwnd ? RGB(26, 140, 255) : RGB(128, 128, 128));
    FrameRect(dc, &rc, edge);
    DeleteObject(edge);
    if (saved)
        RestoreDC(dc, saved);
    if (bmp) {
        BitBlt(dc, 0, 0, MINI(w, lr.right), MINI(h, lr.bottom), mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
    }
    if (mem)
        DeleteDC(mem);
    EndPaint(p->hwnd, &ps);
}

static LRESULT CALLBACK pin_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_NCCREATE) {
        Pin *np = ((CREATESTRUCTW *)lp)->lpCreateParams;
        np->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)np);
    }
    Pin *p = pin_of(h);
    if (!p)
        return DefWindowProcW(h, m, wp, lp);
    switch (m) {
    case WM_NCCALCSIZE:
        if (wp)
            return 0; /* no non-client area: the image is the whole window */
        break;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_NCACTIVATE:
        return DefWindowProcW(h, m, wp, -1); /* do not repaint a frame we do not have */
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = mm->ptMinTrackSize.y = 1;
        mm->ptMaxTrackSize.x = mm->ptMaxTrackSize.y = 32000;
        return 0;
    }
    case WM_SYSCOMMAND: {
        UINT c = (UINT)(wp & 0xFFF0);
        if (c == SC_MAXIMIZE || c == SC_MINIMIZE || c == SC_SIZE || c == SC_RESTORE)
            return 0;
        break;
    }
    case WM_DPICHANGED:
        return 0; /* keep physical pixel size across monitors */
    case WM_ERASEBKGND:
        return 1;
    case WM_ACTIVATE: /* the outline changes color */
        InvalidateRect(h, NULL, FALSE);
        break;
    case WM_PAINT:
        paint(p);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(h);
        p->drag = true;
        GetCursorPos(&p->drag_pt);
        {
            RECT wr;
            GetWindowRect(h, &wr);
            p->win_pt.x = wr.left;
            p->win_pt.y = wr.top;
            p->restore_rect = wr;
        }
        return 0;
    case WM_MOUSEMOVE:
        if (p->drag) {
            POINT c;
            GetCursorPos(&c);
            SetWindowPos(h, NULL, p->win_pt.x + c.x - p->drag_pt.x, p->win_pt.y + c.y - p->drag_pt.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (!P.rack || !IsWindowVisible(P.rack))
                rack_show(h);
        }
        return 0;
    case WM_LBUTTONUP:
        if (p->drag) {
            p->drag = false;
            ReleaseCapture();
            POINT at;
            RECT r;
            GetCursorPos(&at);
            if (P.rack && IsWindowVisible(P.rack) && GetWindowRect(P.rack, &r) && rect_has(&r, at.x, at.y))
                rack_store(p, p->restore_rect);
        }
        return 0;
    case WM_CAPTURECHANGED:
        p->drag = false;
        return 0;
    case WM_LBUTTONDBLCLK:
        DestroyWindow(h);
        return 0;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
            set_alpha(p, p->alpha + (d > 0 ? 10 : -10));
        } else {
            POINT c = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            float z = p->zoom * (d > 0 ? 1.1f : 1.f / 1.1f);
            if ((p->zoom < 1.f && z > 1.f) || (p->zoom > 1.f && z < 1.f) || fabsf(z - 1.f) < 0.02f)
                z = 1.f; /* always stop at 100% on the way through */
            set_zoom(p, z, c);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (wp == VK_ESCAPE)
            DestroyWindow(h);
        else if (ctrl && wp == 'C')
            pin_copy(p);
        else if (ctrl && wp == 'S')
            pin_save(p);
        else if (ctrl && wp == 'H') {
            RECT r;
            GetWindowRect(h, &r);
            rack_store(p, r);
        }
        return 0;
    }
    case WM_CONTEXTMENU: {
        POINT at = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (at.x == -1 && at.y == -1) {
            RECT wr;
            GetWindowRect(h, &wr);
            at.x = wr.left;
            at.y = wr.top;
        }
        pin_menu(p, at);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_LABEL) {
            KillTimer(h, TIMER_LABEL);
            p->show_label = false;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_NCDESTROY: {
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        Pin **pp = &P.list;
        while (*pp && *pp != p)
            pp = &(*pp)->next;
        if (*pp) { /* not listed yet = creation failed; pin_add frees it */
            *pp = p->next;
            P.count--;
            img_destroy(&p->img);
            img_destroy(&p->thumb);
            mem_free(p);
            if (!P.list && P.rack)
                DestroyWindow(P.rack);
            else
                rack_refresh();
        }
        return 0;
    }
    }
    return DefWindowProcW(h, m, wp, lp);
}

bool pin_init(HINSTANCE inst, HWND daemon)
{
    P.inst = inst;
    P.daemon = daemon;
    P.top_all = true; /* until the settings say otherwise */
    WNDCLASSW wc = {0};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = pin_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_SIZEALL);
    wc.lpszClassName = L"sniplite.pin";
    if (!RegisterClassW(&wc))
        return false;
    wc.style = 0;
    wc.lpfnWndProc = rack_proc;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"sniplite.rack";
    return RegisterClassW(&wc) != 0;
}

bool pin_add(Img *img, int x, int y, bool at_cursor)
{
    Pin *p = mem_alloc(sizeof *p);
    if (!p) {
        img_destroy(img);
        return false;
    }
    p->img = *img;
    memset(img, 0, sizeof *img);
    p->zoom = 1.f;
    p->alpha = 100;
    int w = p->img.w, h = p->img.h;
    if (at_cursor) {
        POINT c;
        GetCursorPos(&c);
        MONITORINFO mi = {sizeof mi};
        GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST), &mi);
        RECT wa = mi.rcWork;
        float fit = fminf(0.9f * (float)(wa.right - wa.left) / (float)w, 0.9f * (float)(wa.bottom - wa.top) / (float)h);
        if (fit < 1.f) {
            p->zoom = fit;
            w = MAXI(8, (int)lroundf((float)w * fit));
            h = MAXI(8, (int)lroundf((float)h * fit));
        }
        x = CLAMPI((int)c.x - w / 2, (int)wa.left, (int)wa.right - w);
        y = CLAMPI((int)c.y - h / 2, (int)wa.top, (int)wa.bottom - h);
    }
    /* WS_CAPTION (non-client area removed in WM_NCCALCSIZE) gives the native DWM shadow. */
    HWND hw = CreateWindowExW((P.top_all ? WS_EX_TOPMOST : 0) | WS_EX_TOOLWINDOW, L"sniplite.pin", L"sniplite", WS_POPUP | WS_CAPTION, x, y, w,
                              h, NULL, NULL, P.inst, p);
    if (!hw) {
        img_destroy(&p->img);
        mem_free(p);
        return false;
    }
    p->next = P.list;
    P.list = p;
    P.count++;
    MARGINS mg = {0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(hw, &mg);
    DWORD corner = 1; /* DWMWCP_DONOTROUND */
    DwmSetWindowAttribute(hw, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof corner);
    BOOL still = TRUE; /* it is there at once: no opening or closing animation */
    DwmSetWindowAttribute(hw, DWMWA_TRANSITIONS_FORCEDISABLED, &still, sizeof still);
    SetWindowPos(hw, P.top_all ? HWND_TOPMOST : HWND_TOP, x, y, w, h, SWP_FRAMECHANGED | SWP_NOACTIVATE);
    pin_reveal(hw);
    SetForegroundWindow(hw);
    return true;
}

bool pin_any_visible(void)
{
    if (P.rack && IsWindowVisible(P.rack))
        return true;
    for (Pin *p = P.list; p; p = p->next)
        if (IsWindowVisible(p->hwnd))
            return true;
    return false;
}

void pin_toggle_all(void)
{
    bool any_visible = pin_any_visible();
    if (P.rack && P.rack_open)
        ShowWindow(P.rack, any_visible ? SW_HIDE : SW_SHOWNOACTIVATE);
    for (Pin *p = P.list; p; p = p->next) {
        if (any_visible)
            ShowWindow(p->hwnd, SW_HIDE);
        else if (!p->stored)
            pin_reveal(p->hwnd);
    }
}

void pin_close_all(void)
{
    while (P.list)
        DestroyWindow(P.list->hwnd);
}

int pin_count(void) { return P.count; }
