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

# === TELEGRAM BOT ===
bot = telebot.TeleBot(config["token"])
authorized = set()
failed_attempts = {}

# === КНОПКИ ===
markup = types.ReplyKeyboardMarkup(resize_keyboard=True)
markup.add("📂 Открыть программу", "📸 Скриншот")
markup.add("🌐 Скриншот сайта", "ℹ️ Версия")
markup.add("🔄 Обновить бота", "🔁 Откатить")
markup.add("📑 Логи", "/status")

# === ПРИ ЗАПУСКЕ УВЕДОМЛЕНИЕ ===
def notify_admin_on_start():
    for user_id in AUTHORIZED_USERS:
        try:
            bot.send_message(user_id, f"✅ ALT Bot v{BOT_VERSION} запущен и готов к работе.")
        except Exception as e:
            print(f"❌ Не удалось отправить сообщение пользователю {user_id}: {e}")

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

    if message.text == PASSWORD:
        authorized.add(message.from_user.id)
        failed_attempts.pop(message.from_user.id, None)
        logging.info(f"✅ Пользователь {user_id} успешно авторизовался.")
        bot.send_message(message.chat.id, "✅ Доступ разрешён.", reply_markup=markup)
        return

    if message.from_user.id not in authorized:
        failed_attempts[message.from_user.id] = failed_attempts.get(message.from_user.id, 0) + 1
        if failed_attempts[message.from_user.id] >= 3:
            bot.send_message(message.chat.id, "⛔ Доступ заблокирован после 3 неверных попыток.")
            logging.warning(f"🚫 Блокировка пользователя {user_id} после 3 неверных попыток.")
        else:
            bot.send_message(message.chat.id, "❌ Неверный пароль.")
        return

    if message.text == "📂 Открыть программу":
        keyboard = types.InlineKeyboardMarkup()
        for name in PROGRAMS:
            keyboard.add(types.InlineKeyboardButton(name, callback_data=f"open:{name}"))
        bot.send_message(message.chat.id, "Выберите программу:", reply_markup=keyboard)

    elif message.text == "📸 Скриншот":
        from PIL import ImageGrab
        path = f"screenshots/screenshot_{int(time.time())}.png"
        ImageGrab.grab().save(path)
        with open(path, 'rb') as f:
            bot.send_photo(message.chat.id, f)

    elif message.text == "🌐 Скриншот сайта":
        msg = bot.send_message(message.chat.id, "🌍 Введите URL:")
        bot.register_next_step_handler(msg, handle_site_screenshot)

    elif message.text == "ℹ️ Версия":
        bot.send_message(message.chat.id, f"🤖 Текущая версия бота: v{BOT_VERSION}")

    elif message.text == "🔄 Обновить бота":
        from updater_git import backup_bot, update_from_git, restart
        backup_bot()
        if update_from_git():
            bot.send_message(message.chat.id, "🔄 Обновление завершено. Перезапуск...")
            restart()
        else:
            bot.send_message(message.chat.id, "⚠️ Ошибка при обновлении. Откат не требуется.")

    elif message.text == "🔁 Откатить":
        from updater_git import restore_backup, restart
        if restore_backup():
            bot.send_message(message.chat.id, "♻️ Откат выполнен. Перезапуск...")
            restart()
        else:
            bot.send_message(message.chat.id, "❌ Откат не удался.")

    elif message.text == "📑 Логи":
        logs = sorted(os.listdir("logs"), reverse=True)
        if logs:
            with open(os.path.join("logs", logs[0]), 'rb') as f:
                bot.send_document(message.chat.id, f)
        else:
            bot.send_message(message.chat.id, "🗒️ Логи не найдены.")

    elif message.text == "/status":
        cpu = psutil.cpu_percent()
        ram = psutil.virtual_memory().percent
        disk = psutil.disk_usage("/").percent
        bot.send_message(message.chat.id, f"📊 Система: CPU: {cpu}%\nRAM: {ram}%\nDisk: {disk}%")

@bot.callback_query_handler(func=lambda call: call.data.startswith("open:"))
def open_program(call):
    prog_name = call.data.split(":", 1)[1]
    path = PROGRAMS.get(prog_name)
    if path and os.path.exists(path):
        os.startfile(path)
        bot.send_message(call.message.chat.id, f"🚀 {prog_name} запущен.")
    else:
        bot.send_message(call.message.chat.id, f"❌ Не удалось найти {prog_name}.")

def handle_site_screenshot(message):
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
