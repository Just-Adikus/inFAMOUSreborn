"""Менеджер сохранений inFamous (порт inFAMOUSreborn).

Игра знает только два каталога сохранений: BLES00000_INFAMOUS_0 (автосохранение) и BLES00000_INFAMOUS_1
(ручное сохранение). Этот менеджер копирует их в именованные слоты (infamous-recomp/gamedata/save_slots/) и
возвращает обратно. Перед каждой загрузкой текущее состояние автоматически копируется в резервный слот.

Запуск: «Сохранения inFamous.cmd» в папке игры, или: python save_manager.py
"""
import os, sys, shutil, re, time, subprocess, datetime

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)                                   # infamous-recomp
SAVEDATA = os.path.join(PORT, 'gamedata', 'dev_hdd0', 'home', '00000001', 'savedata')
SLOTS = os.path.join(PORT, 'gamedata', 'save_slots')
ACTIVE = ('BLES00000_INFAMOUS_0', 'BLES00000_INFAMOUS_1')     # автосохранение, ручное сохранение
AUTO_PREFIX = 'Резерв'
KEEP_AUTO = 5


# ------------------------------------------------------------------ данные (без интерфейса)
def read_sfo(d):
    """PARAM.SFO порта: текст KEY=VALUE в UTF-8"""
    p = os.path.join(d, 'PARAM.SFO'); out = {}
    try:
        for line in open(p, encoding='utf-8', errors='replace'):
            line = line.rstrip('\r\n')
            if '=' in line:
                k, v = line.split('=', 1); out[k] = v
    except OSError:
        pass
    return out


def save_info(d):
    """(миссия, процент, время изменения) одного каталога сохранения или None"""
    st = os.path.join(d, 'STATE')
    if not os.path.isfile(st):
        return None
    sfo = read_sfo(d)
    mission = sfo.get('SUB_TITLE', '?')
    try: txt = open(os.path.join(d, 'PARAM.SFO'), encoding='utf-8', errors='replace').read()
    except OSError: txt = ''
    m = re.search(r'(Завершено|Completion):\s*(\d+)%', txt)
    pct = (m.group(2) + '%') if m else ''
    return mission, pct, os.path.getmtime(st)


def newest_info(d):
    """информация о самом свежем из двух каталогов (ручное / авто) внутри d"""
    best = None
    for name in ACTIVE:
        i = save_info(os.path.join(d, name))
        if i and (best is None or i[2] > best[2]):
            best = i
    return best


def safe_name(s):
    s = re.sub(r'[\\/:*?"<>|]+', ' ', s).strip().rstrip('.')
    return s[:80] or 'Слот'


def list_slots():
    os.makedirs(SLOTS, exist_ok=True)
    out = []
    for n in os.listdir(SLOTS):
        d = os.path.join(SLOTS, n)
        if os.path.isdir(d):
            i = newest_info(d)
            out.append({'name': n, 'dir': d, 'mission': i[0] if i else '(пусто)',
                        'pct': i[1] if i else '', 'time': i[2] if i else os.path.getmtime(d)})
    out.sort(key=lambda s: os.path.getmtime(s['dir']), reverse=True)   # newest slot first
    return out


def copy_saves(src, dst):
    """копирует оба каталога игры из src в dst (dst очищается от старых копий)"""
    os.makedirs(dst, exist_ok=True)
    n = 0
    for name in ACTIVE:
        s, t = os.path.join(src, name), os.path.join(dst, name)
        if os.path.isdir(t):
            shutil.rmtree(t)
        if os.path.isdir(s):
            shutil.copytree(s, t); n += 1
    return n


def unique_slot_dir(name):
    base = safe_name(name); d = os.path.join(SLOTS, base); k = 2
    while os.path.exists(d):
        d = os.path.join(SLOTS, '%s (%d)' % (base, k)); k += 1
    return d


def store(name):
    """текущие сохранения игры -> новый слот"""
    if not newest_info(SAVEDATA):
        raise RuntimeError('В игре пока нет сохранений.')
    d = unique_slot_dir(name)
    copy_saves(SAVEDATA, d)
    return d


def overwrite(slot_dir):
    if not newest_info(SAVEDATA):
        raise RuntimeError('В игре пока нет сохранений.')
    copy_saves(SAVEDATA, slot_dir)


def load(slot_dir):
    """слот -> сохранения игры; текущие сначала уходят в резервный слот"""
    if not newest_info(slot_dir):
        raise RuntimeError('В этом слоте нет сохранения.')
    backup = None
    if newest_info(SAVEDATA):
        stamp = datetime.datetime.now().strftime('%Y-%m-%d %H-%M-%S')  # sorts by date
        backup = unique_slot_dir('%s %s' % (AUTO_PREFIX, stamp))
        copy_saves(SAVEDATA, backup)
    copy_saves(slot_dir, SAVEDATA)
    prune_auto()
    return backup


def prune_auto():
    auto = [s for s in list_slots() if s['name'].startswith(AUTO_PREFIX)]
    auto.sort(key=lambda s: s['name'], reverse=True)   # the name carries the date
    for s in auto[KEEP_AUTO:]:
        shutil.rmtree(s['dir'], ignore_errors=True)


def rename(slot_dir, new):
    d = unique_slot_dir(new)
    os.rename(slot_dir, d)
    return d


def delete(slot_dir):
    shutil.rmtree(slot_dir)


def game_running():
    if os.name != 'nt':
        return False
    try:
        out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq inFamousRecomp.exe', '/NH'],
                             capture_output=True, text=True, creationflags=0x08000000).stdout
        return 'inFamousRecomp.exe' in out
    except Exception:
        return False


def fmt_time(t):
    return datetime.datetime.fromtimestamp(t).strftime('%d.%m.%Y %H:%M')


# ------------------------------------------------------------------ интерфейс
def main():
    import tkinter as tk
    from tkinter import ttk, messagebox, simpledialog

    root = tk.Tk()
    root.title('Сохранения inFamous')
    root.geometry('1040x500')
    root.minsize(640, 360)

    cur = tk.StringVar()
    top = ttk.Frame(root, padding=(12, 10, 12, 4)); top.pack(fill='x')
    ttk.Label(top, text='Текущее сохранение в игре:', font=('Segoe UI', 10, 'bold')).pack(anchor='w')
    ttk.Label(top, textvariable=cur).pack(anchor='w')

    mid = ttk.Frame(root, padding=(12, 4)); mid.pack(fill='both', expand=True)
    cols = ('mission', 'pct', 'time')
    tv = ttk.Treeview(mid, columns=cols, show='tree headings', selectmode='browse')
    tv.heading('#0', text='Слот'); tv.column('#0', width=250)
    tv.heading('mission', text='Миссия'); tv.column('mission', width=300)
    tv.heading('pct', text='Пройдено'); tv.column('pct', width=80, anchor='center')
    tv.heading('time', text='Дата'); tv.column('time', width=130, anchor='center')
    sb = ttk.Scrollbar(mid, orient='vertical', command=tv.yview); tv.configure(yscrollcommand=sb.set)
    tv.pack(side='left', fill='both', expand=True); sb.pack(side='right', fill='y')

    status = tk.StringVar()
    ttk.Label(root, textvariable=status, padding=(12, 2), foreground='#555').pack(fill='x')

    def refresh(select=None):
        i = newest_info(SAVEDATA)
        cur.set('%s   %s   (%s)' % (i[0], i[1], fmt_time(i[2])) if i else 'нет сохранений')
        tv.delete(*tv.get_children())
        for s in list_slots():
            tv.insert('', 'end', iid=s['dir'], text=s['name'], values=(s['mission'], s['pct'], fmt_time(s['time'])))
        if select and tv.exists(select):
            tv.selection_set(select); tv.see(select)

    def selected():
        sel = tv.selection()
        if not sel:
            messagebox.showinfo('Сохранения inFamous', 'Сначала выберите слот в списке.'); return None
        return sel[0]

    def guard(action):
        if game_running():
            return messagebox.askyesno('Игра запущена',
                'Игра сейчас открыта.\n\n' + action +
                '\n\nПродолжить?', icon='warning')
        return True

    def do_store():
        i = newest_info(SAVEDATA)
        if not i:
            messagebox.showinfo('Сохранения inFamous', 'В игре пока нет сохранений.'); return
        name = simpledialog.askstring('Новый слот', 'Название слота:', initialvalue=i[0], parent=root)
        if not name:
            return
        try:
            d = store(name); status.set('Сохранено в слот «%s».' % os.path.basename(d)); refresh(d)
        except Exception as e:
            messagebox.showerror('Ошибка', str(e))

    def do_overwrite():
        d = selected()
        if not d:
            return
        if not messagebox.askyesno('Перезаписать слот',
                'Записать текущее сохранение игры в слот «%s»?\nСтарое содержимое слота будет заменено.' % os.path.basename(d)):
            return
        try:
            overwrite(d); status.set('Слот «%s» перезаписан.' % os.path.basename(d)); refresh(d)
        except Exception as e:
            messagebox.showerror('Ошибка', str(e))

    def do_load():
        d = selected()
        if not d:
            return
        if not guard('Если загрузить слот при открытой игре, после этого сразу выберите в игре «Загрузка» '
                     '(или перезапустите игру) и не сохраняйтесь до загрузки, иначе игра перезапишет слот своим прогрессом.'):
            return
        if not messagebox.askyesno('Загрузить слот',
                'Загрузить слот «%s» в игру?\n\nТекущее сохранение игры будет автоматически скопировано '
                'в слот «%s <дата>».' % (os.path.basename(d), AUTO_PREFIX)):
            return
        try:
            b = load(d)
            status.set('Слот «%s» загружен в игру.%s' % (os.path.basename(d),
                       (' Предыдущее состояние: «%s».' % os.path.basename(b)) if b else ''))
            refresh(d)
        except Exception as e:
            messagebox.showerror('Ошибка', str(e))

    def do_rename():
        d = selected()
        if not d:
            return
        name = simpledialog.askstring('Переименовать', 'Новое название:', initialvalue=os.path.basename(d), parent=root)
        if not name or safe_name(name) == os.path.basename(d):
            return
        try:
            nd = rename(d, name); refresh(nd)
        except Exception as e:
            messagebox.showerror('Ошибка', str(e))

    def do_delete():
        d = selected()
        if not d:
            return
        if messagebox.askyesno('Удалить слот', 'Удалить слот «%s»? Это нельзя отменить.' % os.path.basename(d), icon='warning'):
            try:
                delete(d); status.set('Слот удалён.'); refresh()
            except Exception as e:
                messagebox.showerror('Ошибка', str(e))

    def do_open():
        os.makedirs(SLOTS, exist_ok=True)
        if os.name == 'nt':
            os.startfile(SLOTS)

    bar = ttk.Frame(root, padding=(12, 6, 12, 12)); bar.pack(fill='x')
    for text, cmd in (('Сохранить в новый слот', do_store), ('Перезаписать слот', do_overwrite),
                      ('Загрузить слот', do_load), ('Переименовать', do_rename), ('Удалить', do_delete),
                      ('Открыть папку', do_open), ('Обновить', lambda: refresh())):
        ttk.Button(bar, text=text, command=cmd).pack(side='left', padx=(0, 6))
    tv.bind('<Double-1>', lambda e: do_load())

    refresh()
    root.mainloop()


if __name__ == '__main__':
    main()
