import os
import json
import logging
import pyautogui
import subprocess
import telebot
from telebot import types
from datetime import datetime
from selenium import webdriver
from selenium.webdriver.chrome.service import Service

# Переименовываем консоль
os.system("title TelegramBot by You")

# Загрузка конфигурации
with open("config.json", "r") as f:
    config = json.load(f)

TOKEN = config["bot_token"]
PROGRAMS = config["programs"]
CHROMEDRIVER_PATH = config["chromedriver_path"]

# Инициализация логирования
os.makedirs("logs", exist_ok=True)
logging.basicConfig(filename="logs/log.txt", level=logging.INFO,
                    format="%(asctime)s - %(levelname)s - %(message)s")

# Создание папки для скриншотов
os.makedirs("screenshots", exist_ok=True)

bot = telebot.TeleBot(TOKEN)

# Кнопки
def main_keyboard():
    markup = types.ReplyKeyboardMarkup(resize_keyboard=True)
    markup.add("📂 Запустить программу", "📸 Скриншот экрана")
    markup.add("🌐 Поиск в браузере")
    return markup

@bot.message_handler(commands=["start"])
def start(message):
    bot.send_message(message.chat.id, "Привет! Выбери действие:", reply_markup=main_keyboard())

@bot.message_handler(func=lambda m: m.text == "📂 Запустить программу")
def choose_program(message):
    markup = types.ReplyKeyboardMarkup(resize_keyboard=True)
    for name in PROGRAMS:
        markup.add(name)
    markup.add("🔙 Назад")
    bot.send_message(message.chat.id, "Выберите программу для запуска:", reply_markup=markup)

@bot.message_handler(func=lambda m: m.text in PROGRAMS)
def run_program(message):
    path = PROGRAMS[message.text]
    try:
        subprocess.Popen(path)
        logging.info(f"Открыта программа: {message.text}")
        bot.send_message(message.chat.id, f"✅ Программа {message.text} запущена.")
    except Exception as e:
        logging.error(f"Ошибка запуска {message.text}: {e}")
        bot.send_message(message.chat.id, f"❌ Ошибка запуска: {e}")

@bot.message_handler(func=lambda m: m.text == "📸 Скриншот экрана")
def screenshot(message):
    try:
        filename = datetime.now().strftime("screenshots/screen_%Y%m%d_%H%M%S.png")
        pyautogui.screenshot(filename)
        logging.info("Сделан скриншот.")
        with open(filename, "rb") as photo:
            bot.send_photo(message.chat.id, photo)
    except Exception as e:
        logging.error(f"Ошибка скриншота: {e}")
        bot.send_message(message.chat.id, f"❌ Ошибка скриншота: {e}")

@bot.message_handler(func=lambda m: m.text == "🌐 Поиск в браузере")
def search_prompt(message):
    msg = bot.send_message(message.chat.id, "Введите поисковый запрос:")
    bot.register_next_step_handler(msg, browser_search)

def browser_search(message):
    query = message.text
    url = f"https://www.google.com/search?q={query}"
    try:
        service = Service(CHROMEDRIVER_PATH)
        options = webdriver.ChromeOptions()
        options.add_argument('--headless')
        options.add_argument('--window-size=1280,720')
        driver = webdriver.Chrome(service=service, options=options)
        driver.get(url)
        screenshot_path = f"screenshots/search_{datetime.now().strftime('%Y%m%d_%H%M%S')}.png"
        driver.save_screenshot(screenshot_path)
        driver.quit()
        logging.info(f"Выполнен поиск: {query}")
        with open(screenshot_path, "rb") as photo:
            bot.send_photo(message.chat.id, photo, caption=f"🔍 Результат для запроса: {query}")
    except Exception as e:
        logging.error(f"Ошибка при поиске: {e}")
        bot.send_message(message.chat.id, f"❌ Ошибка при открытии браузера: {e}")

@bot.message_handler(func=lambda m: m.text == "🔙 Назад")
def go_back(message):
    bot.send_message(message.chat.id, "Главное меню:", reply_markup=main_keyboard())

# Запуск
bot.polling(none_stop=True)
