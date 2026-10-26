/* ============================================================================================================
 * RU-patch: in-game SAVE SLOTS (SAVEDATA_SLOTS, default 1; 0 = the old "Overwrite this save?" box).
 *
 * The game itself knows one manual save directory (and one autosave directory) per title. When it saves or
 * loads through the List selector, this shows a slot picker instead of the yes/no box:
 *   Save: "+ New slot" or an existing slot (overwrite asks first). The game writes its own directory as
 *         usual; after a successful save its save directories are copied into the slot.
 *   Load: "Current save" (as before) or a slot; the slot is copied into the game's save directories first
 *         (the current ones go to a "Резерв <date>" slot, the last 5 are kept), then the game loads it.
 * Slots live in gamedata\save_slots\<name>\<save dirs> -- the same layout as tools\save_manager.py, so both
 * see the same slots. Save directories here are flat (PARAM.SFO, ICON0.PNG, STATE), so copies are flat too.
 * ============================================================================================================ */
#ifndef SAVEDATA_SLOTS_H
#define SAVEDATA_SLOTS_H
#ifdef _WIN32
#include <wchar.h>

#define SS_MAX 256
#define SS_KEEP_RESERVE 5
static const wchar_t SS_RESERVE[] = L"Резерв";

typedef struct { wchar_t name[200]; wchar_t mission[200]; wchar_t pct[16]; FILETIME t; } SsSlot;
static wchar_t ss_root[1024], ss_slots[1024];
static SsSlot ss_list[SS_MAX]; static int ss_n = 0;
static int ss_pending = 0;                 /* save: 1 = into a new slot, 2 = into ss_pending_dir */
static wchar_t ss_pending_dir[1024];

static int ss_enabled(void)
{
    const char* e = getenv("SAVEDATA_SLOTS");
    return !(e && e[0] == '0');
}

static void ss_init(const char* save_root)
{
    if (ss_slots[0]) return;
    wchar_t r[1024], tmp[1200];
    MultiByteToWideChar(CP_UTF8, 0, save_root, -1, r, 1024);
    for (wchar_t* p = r; *p; p++) if (*p == L'/') *p = L'\\';
    GetFullPathNameW(r, 1024, ss_root, NULL);
    swprintf(tmp, 1200, L"%ls\\..\\..\\..\\..\\save_slots", ss_root);   /* gamedata\dev_hdd0\home\<user>\savedata -> gamedata */
    GetFullPathNameW(tmp, 1024, ss_slots, NULL);
    CreateDirectoryW(ss_slots, NULL);
}

/* "BLES00000_INFAMOUS_1" -> "BLES00000_INFAMOUS_" (the game's manual + auto save directories share it) */
static void ss_prefix(const char* dirName, wchar_t* out, int cap)
{
    MultiByteToWideChar(CP_UTF8, 0, dirName, -1, out, cap);
    size_t n = wcslen(out);
    while (n > 0 && out[n - 1] >= L'0' && out[n - 1] <= L'9') out[--n] = 0;
}

static int ss_starts(const wchar_t* s, const wchar_t* p) { return wcsncmp(s, p, wcslen(p)) == 0; }

static void ss_rm_flat(const wchar_t* d)
{
    wchar_t q[1100], f[1100]; WIN32_FIND_DATAW fd;
    swprintf(q, 1100, L"%ls\\*", d);
    HANDLE h = FindFirstFileW(q, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                 swprintf(f, 1100, L"%ls\\%ls", d, fd.cFileName); SetFileAttributesW(f, FILE_ATTRIBUTE_NORMAL); DeleteFileW(f); } }
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(d);
}

static void ss_copy_flat(const wchar_t* src, const wchar_t* dst)
{
    wchar_t q[1100], a[1100], b[1100]; WIN32_FIND_DATAW fd;
    CreateDirectoryW(dst, NULL);
    swprintf(q, 1100, L"%ls\\*", src);
    HANDLE h = FindFirstFileW(q, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
             swprintf(a, 1100, L"%ls\\%ls", src, fd.cFileName); swprintf(b, 1100, L"%ls\\%ls", dst, fd.cFileName);
             CopyFileW(a, b, FALSE); } }
    while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* make dst's <prefix>* directories an exact copy of src's */
static int ss_mirror(const wchar_t* src, const wchar_t* dst, const wchar_t* prefix)
{
    wchar_t q[1100], a[1100], b[1100]; WIN32_FIND_DATAW fd; int n = 0;
    CreateDirectoryW(dst, NULL);
    swprintf(q, 1100, L"%ls\\%ls*", dst, prefix);                       /* drop what src does not have */
    HANDLE h = FindFirstFileW(q, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                 swprintf(b, 1100, L"%ls\\%ls", dst, fd.cFileName); ss_rm_flat(b); } }
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    swprintf(q, 1100, L"%ls\\%ls*", src, prefix);
    h = FindFirstFileW(q, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do { if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') {
             swprintf(a, 1100, L"%ls\\%ls", src, fd.cFileName); swprintf(b, 1100, L"%ls\\%ls", dst, fd.cFileName);
             ss_copy_flat(a, b); n++; } }
    while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

/* the newest save directory under root: mission (SUB_TITLE), "31%", STATE time; 0 = none */
static int ss_info(const wchar_t* root, const wchar_t* prefix, wchar_t* mission, wchar_t* pct, FILETIME* t)
{
    wchar_t q[1100], p[1100]; WIN32_FIND_DATAW fd; int found = 0; FILETIME best = { 0, 0 }; wchar_t bestdir[300] = L"";
    swprintf(q, 1100, L"%ls\\%ls*", root, prefix);
    HANDLE h = FindFirstFileW(q, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        WIN32_FILE_ATTRIBUTE_DATA ad;
        swprintf(p, 1100, L"%ls\\%ls\\STATE", root, fd.cFileName);
        if (!GetFileAttributesExW(p, GetFileExInfoStandard, &ad)) continue;
        if (!found || CompareFileTime(&ad.ftLastWriteTime, &best) > 0) { best = ad.ftLastWriteTime; wcsncpy(bestdir, fd.cFileName, 299); found = 1; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!found) return 0;
    if (t) *t = best;
    mission[0] = 0; pct[0] = 0;
    swprintf(p, 1100, L"%ls\\%ls\\PARAM.SFO", root, bestdir);
    FILE* f = _wfopen(p, L"rb");
    if (f) {
        char buf[4096]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
        const char* s = strstr(buf, "SUB_TITLE=");
        if (s) { s += 10; const char* e = s; while (*e && *e != '\r' && *e != '\n') e++;
                 int m = MultiByteToWideChar(CP_UTF8, 0, s, (int)(e - s), mission, 199); mission[m < 0 ? 0 : m] = 0; }
        static const char k_ru[] = "\xD0\x97\xD0\xB0\xD0\xB2\xD0\xB5\xD1\x80\xD1\x88\xD0\xB5\xD0\xBD\xD0\xBE: ";   /* Завершено: */
        const char* c = strstr(buf, k_ru);
        if (c) c += sizeof k_ru - 1; else if ((c = strstr(buf, "Completion: "))) c += 12;
        if (c) { int k = 0; while (k < 3 && c[k] >= '0' && c[k] <= '9') { pct[k] = (wchar_t)c[k]; k++; } if (k) { pct[k++] = L'%'; } pct[k] = 0; }
    }
    return 1;
}

static int ss_cmp(const void* a, const void* b)
{
    return CompareFileTime(&((const SsSlot*)b)->t, &((const SsSlot*)a)->t);   /* newest first */
}

static void ss_scan(const wchar_t* prefix)
{
    wchar_t q[1100], d[1100]; WIN32_FIND_DATAW fd;
    ss_n = 0;
    swprintf(q, 1100, L"%ls\\*", ss_slots);
    HANDLE h = FindFirstFileW(q, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.' || ss_n >= SS_MAX) continue;
        SsSlot* s = &ss_list[ss_n];
        wcsncpy(s->name, fd.cFileName, 199); s->name[199] = 0;
        swprintf(d, 1100, L"%ls\\%ls", ss_slots, fd.cFileName);
        if (!ss_info(d, prefix, s->mission, s->pct, &s->t)) { wcscpy(s->mission, L"(пусто)"); s->pct[0] = 0; s->t = fd.ftLastWriteTime; }
        ss_n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(ss_list, ss_n, sizeof ss_list[0], ss_cmp);
}

static void ss_fmt_time(FILETIME t, wchar_t* out, int cap)
{
    FILETIME lt; SYSTEMTIME st;
    FileTimeToLocalFileTime(&t, &lt); FileTimeToSystemTime(&lt, &st);
    swprintf(out, cap, L"%02d.%02d.%04d %02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
}

static void ss_clean_name(wchar_t* s)
{
    for (wchar_t* p = s; *p; p++) if (wcschr(L"\\/:*?\"<>|", *p)) *p = L' ';
    size_t n = wcslen(s); while (n && (s[n - 1] == L' ' || s[n - 1] == L'.')) s[--n] = 0;
    if (!*s) wcscpy(s, L"Слот");
}

/* a free directory name under the slots folder */
static void ss_unique(const wchar_t* want, wchar_t* path, int cap)
{
    wchar_t base[220]; wcsncpy(base, want, 120); base[120] = 0; ss_clean_name(base);
    swprintf(path, cap, L"%ls\\%ls", ss_slots, base);
    for (int k = 2; GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES && k < 1000; k++)
        swprintf(path, cap, L"%ls\\%ls (%d)", ss_slots, base, k);
}

static void ss_prune_reserves(const wchar_t* prefix)
{
    ss_scan(prefix);
    int seen = 0; wchar_t d[1100];
    /* ss_list is newest first; reserve names carry the date, so this keeps the newest ones */
    for (int i = 0; i < ss_n; i++)
        if (ss_starts(ss_list[i].name, SS_RESERVE) && ++seen > SS_KEEP_RESERVE) {
            swprintf(d, 1100, L"%ls\\%ls", ss_slots, ss_list[i].name);
            wchar_t q[1100]; WIN32_FIND_DATAW fd; swprintf(q, 1100, L"%ls\\*", d);
            HANDLE h = FindFirstFileW(q, &fd);
            if (h != INVALID_HANDLE_VALUE) { do { if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') {
                                                       wchar_t s2[1100]; swprintf(s2, 1100, L"%ls\\%ls", d, fd.cFileName); ss_rm_flat(s2); } }
                                             while (FindNextFileW(h, &fd)); FindClose(h); }
            RemoveDirectoryW(d);
        }
}

/* ---------------------------------------------------------------- the picker window */
enum { SS_ID_LIST = 100, SS_ID_OK = 101, SS_ID_DEL = 102, SS_ID_CANCEL = 103 };
static struct { int is_save, done, result, sel; HWND wnd, list; const wchar_t* prefix; HFONT font; } ss_ui;

static void ss_fill(void)
{
    SendMessageW(ss_ui.list, LB_RESETCONTENT, 0, 0);
    wchar_t line[700], tm[40], mis[200], pct[16]; FILETIME t;
    if (ss_ui.is_save) SendMessageW(ss_ui.list, LB_ADDSTRING, 0, (LPARAM)L"+  Новый слот");
    else {
        if (ss_info(ss_root, ss_ui.prefix, mis, pct, &t)) { ss_fmt_time(t, tm, 40); swprintf(line, 700, L"Текущее сохранение\t%ls\t%ls\t%ls", mis, pct, tm); }
        else wcscpy(line, L"Текущее сохранение\t(нет)");
        SendMessageW(ss_ui.list, LB_ADDSTRING, 0, (LPARAM)line);
    }
    for (int i = 0; i < ss_n; i++) {
        ss_fmt_time(ss_list[i].t, tm, 40);
        swprintf(line, 700, L"%ls\t%ls\t%ls\t%ls", ss_list[i].name, ss_list[i].mission, ss_list[i].pct, tm);
        SendMessageW(ss_ui.list, LB_ADDSTRING, 0, (LPARAM)line);
    }
    SendMessageW(ss_ui.list, LB_SETCURSEL, 0, 0);
}

static void ss_finish(int ok)
{
    ss_ui.sel = (int)SendMessageW(ss_ui.list, LB_GETCURSEL, 0, 0);
    ss_ui.result = ok && ss_ui.sel >= 0; ss_ui.done = 1;
}

static void ss_delete_selected(void)
{
    int i = (int)SendMessageW(ss_ui.list, LB_GETCURSEL, 0, 0) - 1;   /* row 0 = new slot / current save */
    if (i < 0 || i >= ss_n) { MessageBoxW(ss_ui.wnd, L"Выберите слот в списке (не первую строку).", L"Слоты сохранений", MB_OK | MB_ICONINFORMATION); return; }
    wchar_t msg[400]; swprintf(msg, 400, L"Удалить слот «%ls»?\nЭто нельзя отменить.", ss_list[i].name);
    if (MessageBoxW(ss_ui.wnd, msg, L"Удалить слот", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    wchar_t d[1100], q[1100], s2[1100]; WIN32_FIND_DATAW fd;
    swprintf(d, 1100, L"%ls\\%ls", ss_slots, ss_list[i].name); swprintf(q, 1100, L"%ls\\*", d);
    HANDLE h = FindFirstFileW(q, &fd);
    if (h != INVALID_HANDLE_VALUE) { do { if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') {
                                              swprintf(s2, 1100, L"%ls\\%ls", d, fd.cFileName); ss_rm_flat(s2); } }
                                     while (FindNextFileW(h, &fd)); FindClose(h); }
    RemoveDirectoryW(d);
    ss_scan(ss_ui.prefix); ss_fill();
}

static LRESULT CALLBACK ss_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(wp) == SS_ID_OK) ss_finish(1);
        else if (LOWORD(wp) == SS_ID_CANCEL) ss_finish(0);
        else if (LOWORD(wp) == SS_ID_DEL) ss_delete_selected();
        else if (LOWORD(wp) == SS_ID_LIST && HIWORD(wp) == LBN_DBLCLK) ss_finish(1);
        return 0;
    case WM_CLOSE: ss_finish(0); return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

/* 1 = accepted, ss_ui.sel = row (0 = new slot / current save, i + 1 = ss_list[i]) */
static int ss_pick(int is_save, const wchar_t* prefix)
{
    static int reg = 0;
    HINSTANCE hi = GetModuleHandleW(NULL);
    if (!reg) {
        WNDCLASSW wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = ss_proc; wc.hInstance = hi; wc.lpszClassName = L"IFSaveSlots";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc); reg = 1;
    }
    memset(&ss_ui, 0, sizeof ss_ui);
    ss_ui.is_save = is_save; ss_ui.prefix = prefix;
    ss_scan(prefix);
    HWND owner = GetForegroundWindow();
    const int W = 900, H = 460;
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    ss_ui.wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"IFSaveSlots",
                                is_save ? L"inFamous - Сохранение: выберите слот" : L"inFamous - Загрузка: выберите слот",
                                WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                (wa.left + wa.right - W) / 2, (wa.top + wa.bottom - H) / 2, W, H, owner, NULL, hi, NULL);
    if (!ss_ui.wnd) return -1;
    ss_ui.font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    RECT rc; GetClientRect(ss_ui.wnd, &rc);
    const int cw = rc.right, ch = rc.bottom, bw = 150, bh = 32, m = 12;
    HWND lbl = CreateWindowExW(0, L"STATIC",
                   is_save ? L"Куда сохранить игру? «Новый слот» не трогает остальные сохранения."
                           : L"Что загрузить? Текущее сохранение игры перед загрузкой слота копируется в слот «Резерв».",
                   WS_CHILD | WS_VISIBLE, m, m, cw - 2 * m, 22, ss_ui.wnd, NULL, hi, NULL);
    ss_ui.list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_USETABSTOPS | LBS_NOINTEGRALHEIGHT,
                   m, m + 30, cw - 2 * m, ch - 30 - bh - 4 * m, ss_ui.wnd, (HMENU)(INT_PTR)SS_ID_LIST, hi, NULL);
    HWND b1 = CreateWindowExW(0, L"BUTTON", is_save ? L"Сохранить" : L"Загрузить", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                   cw - 3 * (bw + m), ch - bh - m, bw, bh, ss_ui.wnd, (HMENU)(INT_PTR)SS_ID_OK, hi, NULL);
    HWND b2 = CreateWindowExW(0, L"BUTTON", L"Удалить слот", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                   cw - 2 * (bw + m), ch - bh - m, bw, bh, ss_ui.wnd, (HMENU)(INT_PTR)SS_ID_DEL, hi, NULL);
    HWND b3 = CreateWindowExW(0, L"BUTTON", L"Отмена", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                   cw - (bw + m), ch - bh - m, bw, bh, ss_ui.wnd, (HMENU)(INT_PTR)SS_ID_CANCEL, hi, NULL);
    HWND all[5] = { lbl, ss_ui.list, b1, b2, b3 };
    for (int i = 0; i < 5; i++) SendMessageW(all[i], WM_SETFONT, (WPARAM)ss_ui.font, TRUE);
    int tabs[3] = { 140, 290, 325 };   /* dialog units: name | mission | % | date */
    SendMessageW(ss_ui.list, LB_SETTABSTOPS, 3, (LPARAM)tabs);
    ss_fill();
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(ss_ui.wnd, SW_SHOW); SetForegroundWindow(ss_ui.wnd); SetFocus(ss_ui.list);
    MSG msg;
    while (!ss_ui.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) { ss_finish(1); continue; }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { ss_finish(0); continue; }
        if (!IsDialogMessageW(ss_ui.wnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    DestroyWindow(ss_ui.wnd); DeleteObject(ss_ui.font);
    return ss_ui.result;
}

/* ---------------------------------------------------------------- hooks used by cellSaveData.c */
/* returns 1 = go on, 0 = cancelled, -1 = slots off / failed (use the old box) */
static int ss_confirm(const char* save_root, const char* dirName, int is_save)
{
    if (!ss_enabled()) return -1;
    ss_init(save_root);
    wchar_t prefix[128]; ss_prefix(dirName, prefix, 128);
    ss_pending = 0;
    int r = ss_pick(is_save, prefix);
    if (r < 0) return -1;
    if (!r) { printf("[cellSaveData] slots: cancelled\n"); return 0; }
    const int i = ss_ui.sel - 1;                       /* -1 = row 0 */
    if (is_save) {
        if (i < 0) { ss_pending = 1; printf("[cellSaveData] slots: save -> new slot\n"); return 1; }
        wchar_t msg[500]; swprintf(msg, 500, L"Перезаписать слот «%ls»\n(%ls)?", ss_list[i].name, ss_list[i].mission);
        if (MessageBoxW(GetForegroundWindow(), msg, L"inFamous - Сохранение", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) != IDYES) return 0;
        swprintf(ss_pending_dir, 1024, L"%ls\\%ls", ss_slots, ss_list[i].name); ss_pending = 2;
        printf("[cellSaveData] slots: save -> existing slot\n");
        return 1;
    }
    if (i < 0) { printf("[cellSaveData] slots: load the current save\n"); return 1; }
    wchar_t mis[200], pct[16], tm[40], bk[1024], src[1024], nm[120]; FILETIME t;
    swprintf(src, 1024, L"%ls\\%ls", ss_slots, ss_list[i].name);
    if (ss_info(ss_root, prefix, mis, pct, &t)) {     /* keep what the game has now */
        SYSTEMTIME st; GetLocalTime(&st);
        swprintf(nm, 120, L"%ls %04d-%02d-%02d %02d-%02d-%02d", SS_RESERVE, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        ss_unique(nm, bk, 1024);
        ss_mirror(ss_root, bk, prefix);
    }
    (void)tm;
    int n = ss_mirror(src, ss_root, prefix);
    ss_prune_reserves(prefix);
    printf("[cellSaveData] slots: slot copied into the game's save (%d dirs), loading it\n", n);
    return 1;
}

static void ss_after_save(const char* dirName, int ok)
{
    if (!ss_pending) return;
    const int mode = ss_pending; ss_pending = 0;
    if (!ok) { printf("[cellSaveData] slots: the save failed, slot untouched\n"); return; }
    wchar_t prefix[128]; ss_prefix(dirName, prefix, 128);
    wchar_t dst[1024];
    if (mode == 1) {
        wchar_t mis[200], pct[16], nm[260]; FILETIME t; SYSTEMTIME st; GetLocalTime(&st);
        if (!ss_info(ss_root, prefix, mis, pct, &t)) wcscpy(mis, L"Сохранение");
        swprintf(nm, 260, L"%ls  %02d.%02d %02d-%02d", mis, st.wDay, st.wMonth, st.wHour, st.wMinute);
        ss_unique(nm, dst, 1024);
    } else wcscpy(dst, ss_pending_dir);
    int n = ss_mirror(ss_root, dst, prefix);
    printf("[cellSaveData] slots: save copied into the slot (%d dirs)\n", n);
}

#endif /* _WIN32 */
#endif /* SAVEDATA_SLOTS_H */
