import os
import json
import logging
import subprocess
import pyautogui
import telebot
from telebot import types
from datetime import datetime
import sys

# === GUI в терминале ===
os.system("title TelegramBot Console GUI")
print("="*50)
print("🟢 TelegramBot Console by You")

# === Загрузка конфигурации ===
with open("config.json", "r") as f:
    config = json.load(f)

TOKEN = config["bot_token"]
ALLOWED_USERS = config["allowed_users"]
PASSWORD = config["password"]
DEBUG = config["debug"]
PROGRAMS = config["programs"]
CHROMEDRIVER_PATH = config["chromedriver_path"]
GITHUB_RAW_URL = config["github_raw_url"]

# === Логирование ===
os.makedirs("logs", exist_ok=True)
log_level = logging.DEBUG if DEBUG else logging.INFO
logging.basicConfig(filename="logs/log.txt", level=log_level, format="%(asctime)s - %(levelname)s - %(message)s")

print("🔑 Авторизованные ID:", ALLOWED_USERS)
print("🐞 DEBUG:", DEBUG)
print("🖥️ Программы:", ", ".join(PROGRAMS.keys()))
print("="*50)

# === Telegram Bot ===
bot = telebot.TeleBot(TOKEN)
AUTHORIZED_USERS = set()

# === Парольная авторизация ===
@bot.message_handler(commands=["start"])
def start(message):
    if message.from_user.id not in ALLOWED_USERS:
        bot.send_message(message.chat.id, "⛔ Доступ запрещён.")
        return
    if message.from_user.id in AUTHORIZED_USERS:
        main_menu(message)
    else:
        msg = bot.send_message(message.chat.id, "🔐 Введите пароль:")
        bot.register_next_step_handler(msg, check_password)

def check_password(message):
    if message.text == PASSWORD:
        AUTHORIZED_USERS.add(message.from_user.id)
        bot.send_message(message.chat.id, "✅ Успешно авторизован!")
        main_menu(message)
    else:
        bot.send_message(message.chat.id, "❌ Неверный пароль.")

# === Главное меню ===
def main_menu(message):
    markup = types.ReplyKeyboardMarkup(resize_keyboard=True)
    markup.add("📂 Запустить программу", "📸 Скриншот экрана")
    markup.add("🌐 Поиск в браузере", "🔄 Обновить бота")
    bot.send_message(message.chat.id, "Выбери действие:", reply_markup=markup)

# === Кнопка: запуск программ ===
@bot.message_handler(func=lambda m: m.text == "📂 Запустить программу")
def choose_program(message):
    if message.from_user.id not in AUTHORIZED_USERS:
        return
    markup = types.ReplyKeyboardMarkup(resize_keyboard=True)
    for name in PROGRAMS:
        markup.add(name)
    markup.add("🔙 Назад")
    bot.send_message(message.chat.id, "Выберите программу для запуска:", reply_markup=markup)

@bot.message_handler(func=lambda m: m.text in PROGRAMS)
def run_program(message):
    if message.from_user.id not in AUTHORIZED_USERS:
        return
    path = PROGRAMS[message.text]
    try:
        subprocess.Popen(path)
        logging.info(f"Открыта программа: {message.text}")
        bot.send_message(message.chat.id, f"✅ Программа {message.text} запущена.")
    except Exception as e:
        logging.error(f"Ошибка запуска {message.text}: {e}")
        bot.send_message(message.chat.id, f"❌ Ошибка запуска: {e}")

# === Кнопка: скриншот ===
@bot.message_handler(func=lambda m: m.text == "📸 Скриншот экрана")
def screenshot(message):
    if message.from_user.id not in AUTHORIZED_USERS:
        return
    try:
        os.makedirs("screenshots", exist_ok=True)
        filename = datetime.now().strftime("screenshots/screen_%Y%m%d_%H%M%S.png")
        pyautogui.screenshot(filename)
        logging.info("Сделан скриншот.")
        with open(filename, "rb") as photo:
            bot.send_photo(message.chat.id, photo)
    except Exception as e:
        logging.error(f"Ошибка скриншота: {e}")
        bot.send_message(message.chat.id, f"❌ Ошибка скриншота: {e}")

# === Кнопка: поиск в браузере ===
@bot.message_handler(func=lambda m: m.text == "🌐 Поиск в браузере")
def search_prompt(message):
    if message.from_user.id not in AUTHORIZED_USERS:
        return
    msg = bot.send_message(message.chat.id, "Введите поисковый запрос:")
    bot.register_next_step_handler(msg, browser_search)

def browser_search(message):
    from selenium import webdriver
    from selenium.webdriver.chrome.service import Service
    query = message.text
    url = f"https://www.google.com/search?q={query}"
    try:
        service = Service(CHROMEDRIVER_PATH)
        options = webdriver.ChromeOptions()
        options.add_argument('--headless')
        options.add_argument('--window-size=1280,720')
        driver = webdriver.Chrome(service=service, options=options)
        driver.get(url)
        os.makedirs("screenshots", exist_ok=True)
        screenshot_path = f"screenshots/search_{datetime.now().strftime('%Y%m%d_%H%M%S')}.png"
        driver.save_screenshot(screenshot_path)
        driver.quit()
        logging.info(f"Выполнен поиск: {query}")
        with open(screenshot_path, "rb") as photo:
            bot.send_photo(message.chat.id, photo, caption=f"🔍 Результат для запроса: {query}")
    except Exception as e:
        logging.error(f"Ошибка при поиске: {e}")
        bot.send_message(message.chat.id, f"❌ Ошибка при поиске: {e}")

# === Кнопка: обновление бота ===
@bot.message_handler(func=lambda m: m.text == "🔄 Обновить бота")
def update_bot(message):
    if message.from_user.id not in AUTHORIZED_USERS:
        return
    bot.send_message(message.chat.id, "🔁 Обновление началось...")
    import updater
    updater.backup_bot()
    if updater.download_latest():
        bot.send_message(message.chat.id, "✅ Обновление загружено. Перезапуск...")
        updater.restart()
    else:
        bot.send_message(message.chat.id, "❌ Ошибка при обновлении.")

# === Назад ===
@bot.message_handler(func=lambda m: m.text == "🔙 Назад")
def go_back(message):
    if message.from_user.id not in AUTHORIZED_USERS:
        return
    main_menu(message)

# === Запуск бота ===
bot.polling(none_stop=True)
