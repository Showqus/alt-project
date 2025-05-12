import os
import json
import time
import ctypes
import shutil
import subprocess
import logging
import psutil
from datetime import datetime
import telebot
from telebot import types

# === ЗАГРУЗКА КОНФИГА ===
with open("config.json", "r", encoding="utf-8") as f:
    config = json.load(f)

DEBUG_MODE = config.get("debug", False)
PASSWORD = config.get("password")
AUTHORIZED_USERS = config.get("authorized_users", [])
PROGRAMS = config.get("programs", {})

# === ПОЛУЧЕНИЕ ВЕРСИИ ИЗ GIT ===
def get_git_version():
    try:
        version = subprocess.check_output(["git", "describe", "--tags"], stderr=subprocess.DEVNULL).decode().strip()
    except subprocess.CalledProcessError:
        version = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"]).decode().strip()
    return version

BOT_VERSION = get_git_version()

# === УСТАНОВКА ЗАГОЛОВКА КОНСОЛИ ===
ctypes.windll.kernel32.SetConsoleTitleW(f"ALT Bot v{BOT_VERSION}")
print(f"\n🤖 ALT Bot v{BOT_VERSION} запущен!")

# === НАСТРОЙКА ЛОГОВ ===
if not os.path.exists("logs"):
    os.makedirs("logs")
logging.basicConfig(filename=f"logs/{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}.log",
                    level=logging.DEBUG if DEBUG_MODE else logging.INFO,
                    format='%(asctime)s - %(levelname)s - %(message)s')

# === ПРОВЕРКА ДОСТУПНЫХ ПРОГРАММ ===
for name, path in PROGRAMS.items():
    if not os.path.exists(path):
        logging.warning(f"⚠️ Программа '{name}' не найдена по пути: {path}")
    else:
        logging.info(f"✅ Программа '{name}' найдена: {path}")

# === СОЗДАНИЕ ПАПКИ ДЛЯ СКРИНШОТОВ ===
if not os.path.exists("screenshots"):
    os.makedirs("screenshots")

# === ЗАГРУЗКА АВТОРИЗОВАННЫХ ПОЛЬЗОВАТЕЛЕЙ ===
AUTH_USERS_FILE = "authorized_users.json"

def load_authorized_users():
    if os.path.exists(AUTH_USERS_FILE):
        with open(AUTH_USERS_FILE, "r") as f:
            return set(json.load(f))
    return set()

def save_authorized_users():
    with open(AUTH_USERS_FILE, "w") as f:
        json.dump(list(authorized_users), f)

# === TELEGRAM BOT ===
bot = telebot.TeleBot(config["token"])
authorized_users = load_authorized_users()
failed_attempts = {}

# === КНОПКИ ===
main_markup = types.ReplyKeyboardMarkup(resize_keyboard=True)
main_markup.add("📂 Открыть программу", "📸 Скриншот")
main_markup.add("🌐 Скриншот сайта", "ℹ️ Версия")
main_markup.add("🔄 Обновить бота", "🔁 Откатить")
main_markup.add("📑 Логи", "📊 Системный статус")

# === ДИНАМИЧЕСКИЕ КНОПКИ ДЛЯ ПРОГРАММ ===
def get_programs_markup():
    markup = types.ReplyKeyboardMarkup(resize_keyboard=True)
    for name in PROGRAMS.keys():
        markup.add(name)
    markup.add("⬅️ Назад")
    return markup

# === ДИНАМИЧЕСКИЕ КНОПКИ ДЛЯ ЗАКРЫТИЯ ПРОЦЕССОВ ===
def get_process_kill_markup(process_list):
    markup = types.ReplyKeyboardMarkup(resize_keyboard=True)
    for proc_name, pid in process_list:
        markup.add(f"❌ Закрыть {proc_name} ({pid})")
    markup.add("⬅️ Назад")
    return markup

# === ПРИ ЗАПУСКЕ УВЕДОМЛЕНИЕ ===
def notify_admin_on_start():
    for user_id in AUTHORIZED_USERS:
        try:
            bot.send_message(user_id, f"✅ ALT Bot v{BOT_VERSION} запущен и готов к работе.")
        except Exception as e:
            print(f"❌ Не удалось отправить сообщение пользователю {user_id}: {e}")

# === ПРОВЕРКА АВТОРИЗАЦИИ ===
def is_user_authorized(user_id):
    return user_id in authorized_users

# === ОБРАБОТКА СООБЩЕНИЙ ===
@bot.message_handler(commands=['start'])
def start(message):
    user_id = str(message.from_user.id)
    if user_id not in AUTHORIZED_USERS:
        bot.send_message(message.chat.id, "⛔ Доступ запрещен.")
        logging.warning(f"❌ Попытка доступа от неавторизованного пользователя {user_id}")
        return
    bot.send_message(message.chat.id, "🔐 Введите пароль:")

@bot.message_handler(func=lambda m: True)
def handle_message(message):
    user_id = str(message.from_user.id)

    if user_id not in AUTHORIZED_USERS:
        return

    if is_user_authorized(message.from_user.id):
        handle_authorized_message(message)
        return

    # Авторизация по паролю
    if message.text == PASSWORD:
        authorized_users.add(message.from_user.id)
        save_authorized_users()
        failed_attempts.pop(message.from_user.id, None)
        logging.info(f"✅ Пользователь {user_id} успешно авторизовался.")
        bot.send_message(message.chat.id, "✅ Доступ разрешён.", reply_markup=main_markup)
    else:
        failed_attempts[message.from_user.id] = failed_attempts.get(message.from_user.id, 0) + 1
        if failed_attempts[message.from_user.id] >= 3:
            bot.send_message(message.chat.id, "⛔ Доступ заблокирован после 3 неверных попыток.")
            logging.warning(f"🚫 Блокировка пользователя {user_id} после 3 неверных попыток.")
        else:
            bot.send_message(message.chat.id, "❌ Неверный пароль.")

def handle_authorized_message(message):
    text = message.text

    if text == "📂 Открыть программу":
        bot.send_message(message.chat.id, "Выберите программу:", reply_markup=get_programs_markup())

    elif text in PROGRAMS:
        path = PROGRAMS[text]
        if os.path.exists(path):
            os.startfile(path)
            bot.send_message(message.chat.id, f"🚀 {text} запущен.", reply_markup=get_programs_markup())
        else:
            bot.send_message(message.chat.id, f"❌ Не удалось найти {text}.", reply_markup=get_programs_markup())

    elif text == "⬅️ Назад":
        bot.send_message(message.chat.id, "Главное меню:", reply_markup=main_markup)

    elif text == "📸 Скриншот":
        from PIL import ImageGrab
        path = f"screenshots/screenshot_{int(time.time())}.png"
        ImageGrab.grab().save(path)
        with open(path, 'rb') as f:
            bot.send_photo(message.chat.id, f)

    elif text == "🌐 Скриншот сайта":
        msg = bot.send_message(message.chat.id, "🌍 Введите URL:")
        bot.register_next_step_handler(msg, handle_site_screenshot)

    elif text == "ℹ️ Версия":
        bot.send_message(message.chat.id, f"🤖 Текущая версия бота: v{BOT_VERSION}")

    elif text == "🔄 Обновить бота":
        from updater_git import backup_bot, update_from_git, restart
        backup_bot()
        if update_from_git():
            bot.send_message(message.chat.id, "🔄 Обновление завершено. Перезапуск...")
            restart()
        else:
            bot.send_message(message.chat.id, "⚠️ Ошибка при обновлении. Откат не требуется.")

    elif text == "🔁 Откатить":
        from updater_git import restore_backup, restart
        if restore_backup():
            bot.send_message(message.chat.id, "♻️ Откат выполнен. Перезапуск...")
            restart()
        else:
            bot.send_message(message.chat.id, "❌ Откат не удался.")

    elif text == "📑 Логи":
        logs = sorted(os.listdir("logs"), reverse=True)
        if logs:
            with open(os.path.join("logs", logs[0]), 'rb') as f:
                bot.send_document(message.chat.id, f)
        else:
            bot.send_message(message.chat.id, "🗒️ Логи не найдены.")

    elif text == "📊 Системный статус":
        show_system_status(message)

    elif text.startswith("❌ Закрыть "):
        parts = text.replace("❌ Закрыть ", "").strip().split(" (")
        proc_name = parts[0]
        pid = int(parts[1].replace(")", ""))
        killed = kill_process_by_pid(pid)
        if killed:
            bot.send_message(message.chat.id, f"✅ {proc_name} закрыт.", reply_markup=main_markup)
        else:
            bot.send_message(message.chat.id, f"⚠️ Не удалось закрыть {proc_name}.", reply_markup=main_markup)

def show_system_status(message):
    cpu = psutil.cpu_percent()
    ram = psutil.virtual_memory().percent
    disk = psutil.disk_usage("/").percent

    # Получаем топ 5 процессов по CPU + RAM
    proc_list = []
    for proc in psutil.process_iter(['pid', 'name', 'cpu_percent', 'memory_percent']):
        try:
            proc_list.append(proc.info)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass

    # Сортировка по сумме CPU и RAM
    top5 = sorted(proc_list, key=lambda p: p['cpu_percent'] + p['memory_percent'], reverse=True)[:5]

    status_text = f"📊 Система:\nCPU: {cpu}%\nRAM: {ram}%\nDisk: {disk}%\n\n🔥 Топ-5 по нагрузке:\n"
    for p in top5:
        status_text += f"• {p['name']} (PID {p['pid']}) - CPU {p['cpu_percent']}% | RAM {round(p['memory_percent'],1)}%\n"

    bot.send_message(message.chat.id, status_text, reply_markup=get_process_kill_markup([(p['name'], p['pid']) for p in top5]))

def kill_process_by_pid(pid):
    try:
        proc = psutil.Process(pid)
        proc.kill()
        return True
    except:
        return False

def handle_site_screenshot(message):
    if not is_user_authorized(message.from_user.id):
        bot.send_message(message.chat.id, "🔑 Вы не авторизованы.")
        return

    import pyautogui
    import webbrowser
    url = message.text
    bot.send_message(message.chat.id, f"🌐 Открываю {url}...")
    webbrowser.open(url)
    time.sleep(5)
    screenshot = pyautogui.screenshot()
    path = f"screenshots/site_{int(time.time())}.png"
    screenshot.save(path)
    with open(path, 'rb') as f:
        bot.send_photo(message.chat.id, f)

# === ЗАПУСК ===
notify_admin_on_start()
bot.polling(none_stop=True)
