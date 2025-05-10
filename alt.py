import telegram # Этот импорт может быть не нужен, если все типы берутся из telegram.
from telegram import InlineKeyboardButton, InlineKeyboardMarkup, Update
from telegram.constants import ParseMode # ParseMode импортируется отсюда
from telegram.ext import (
    Application,
    CommandHandler,
    CallbackQueryHandler,
    ContextTypes, # Замена для CallbackContext в аннотациях типов
    MessageHandler, # Если будете добавлять обработчики сообщений
    filters # С маленькой буквы
)
import subprocess
import logging
import os
import mss
import mss.tools
import psutil
import time

# Включим логирование
logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                    level=logging.INFO)
logger = logging.getLogger(__name__)

# --- НАСТРОЙКИ БОТА ---
BOT_TOKEN = "5571710712:AAGuAAqCYgiXw8fAg3uaEq-dlNnFCcWFbas" # ЗАМЕНИТЕ НА ВАШ ТОКЕН
ALLOWED_USER_ID = 1314664622         # ЗАМЕНИТЕ НА ВАШ TELEGRAM USER ID

ALLOWED_PROGRAMS = {
    "Блокнот": {
        "cmd": "notepad.exe",
        "process_name": "notepad.exe"
    },
    "Калькулятор": {
        "cmd": "calc.exe",
        "process_name": "calc.exe" # или Calculator.exe, CalculatorApp.exe
    },
    "Проводник": {
        "cmd": "explorer.exe",
        "process_name": "explorer.exe"
    },
}
# --- КОНЕЦ НАСТРОЕК ---

SCREENSHOT_FILENAME = "screenshot.png"

# --- Проверка прав пользователя ---
async def is_user_allowed(update: Update) -> bool: # Функции обработчиков теперь async
    user = update.effective_user
    if not user or user.id != ALLOWED_USER_ID:
        if update.callback_query:
            await update.callback_query.answer("У вас нет доступа к этому боту.", show_alert=True)
        elif update.message:
            await update.message.reply_text("У вас нет доступа к этому боту.")
        logger.warning(f"Несанкционированная попытка доступа от пользователя {user.id if user else 'unknown'} ({user.first_name if user else 'unknown'})")
        return False
    return True

# --- Функции для создания клавиатур ---
def get_main_menu_keyboard() -> InlineKeyboardMarkup:
    keyboard = [
        [InlineKeyboardButton("🚀 Запустить программу", callback_data='menu_run')],
        [InlineKeyboardButton("🔄 Перезапустить программу", callback_data='menu_restart')],
        [InlineKeyboardButton("📸 Сделать скриншот", callback_data='menu_screenshot')],
    ]
    return InlineKeyboardMarkup(keyboard)

def get_programs_keyboard(action_prefix: str) -> InlineKeyboardMarkup:
    keyboard = []
    for alias, details in ALLOWED_PROGRAMS.items():
        display_name = details.get("display_name", alias)
        keyboard.append([InlineKeyboardButton(display_name, callback_data=f'{action_prefix}:{alias}')])
    keyboard.append([InlineKeyboardButton("⬅️ Назад в главное меню", callback_data='menu_main')])
    return InlineKeyboardMarkup(keyboard)

# --- Обработчики команд ---
async def start_command(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None: # async и ContextTypes
    if not await is_user_allowed(update): return # await для async функции
    user = update.effective_user
    await update.message.reply_text( # await для асинхронных вызовов API
        f"Привет, {user.first_name}!\n"
        "Выберите действие:",
        reply_markup=get_main_menu_keyboard()
    )

# --- Логика для операций с программами ---
async def _send_message_or_edit(query: telegram.CallbackQuery | None,
                                chat_id: int,
                                bot: telegram.Bot,
                                text_to_send: str,
                                reply_markup: InlineKeyboardMarkup | None = None):
    try:
        if query:
            await query.edit_message_text(text=text_to_send, reply_markup=reply_markup, parse_mode=ParseMode.HTML)
        elif chat_id and bot:
            await bot.send_message(chat_id=chat_id, text=text_to_send, reply_markup=reply_markup, parse_mode=ParseMode.HTML)
        else:
            logger.error("Невозможно отправить сообщение: нет query, chat_id или bot.")
    except telegram.error.BadRequest as e:
        if "Message is not modified" in str(e):
            logger.info("Сообщение не изменено, пропуск редактирования.")
            if query and reply_markup:
                 try:
                     await query.edit_message_reply_markup(reply_markup=reply_markup)
                 except Exception as e_markup:
                     logger.error(f"Не удалось обновить только клавиатуру: {e_markup}")
        else:
            logger.error(f"Ошибка BadRequest при отправке/редактировании сообщения: {e} (текст: {text_to_send})")
            if chat_id and bot: # Запасной вариант
                try:
                    await bot.send_message(chat_id=chat_id, text=text_to_send, reply_markup=reply_markup, parse_mode=ParseMode.HTML)
                except Exception as e_send:
                    logger.error(f"Не удалось отправить сообщение после BadRequest: {e_send}")

    except Exception as e:
        logger.error(f"Общая ошибка при отправке/редактировании сообщения: {e}")
        if chat_id and bot:
            try:
                await bot.send_message(chat_id=chat_id, text=text_to_send, reply_markup=reply_markup, parse_mode=ParseMode.HTML)
            except Exception as e_send_final:
                logger.error(f"Не удалось отправить сообщение после общей ошибки: {e_send_final}")


async def _perform_launch_program(program_alias: str, chat_id: int, bot: telegram.Bot, query: telegram.CallbackQuery = None) -> None:
    if program_alias not in ALLOWED_PROGRAMS:
        await _send_message_or_edit(query, chat_id, bot, f"Программа '{program_alias}' не найдена.")
        return

    program_info = ALLOWED_PROGRAMS[program_alias]
    command_to_run = program_info['cmd']
    display_name = program_info.get("display_name", program_alias)

    try:
        # Запуск процесса остается синхронным
        if isinstance(command_to_run, list):
            subprocess.Popen(command_to_run)
        else:
            subprocess.Popen([command_to_run])
        logger.info(f"Команда запуска для '{display_name}' ('{command_to_run}') выполнена.")
        await _send_message_or_edit(query, chat_id, bot, f"Программа '<b>{display_name}</b>' запущена.", reply_markup=get_main_menu_keyboard())
    except FileNotFoundError:
        msg = f"Ошибка: Файл программы для '<b>{display_name}</b>' ('{command_to_run}') не найден."
        logger.error(msg)
        await _send_message_or_edit(query, chat_id, bot, msg, reply_markup=get_main_menu_keyboard())
    except Exception as e:
        msg = f"Не удалось запустить программу '<b>{display_name}</b>': {e}"
        logger.error(msg)
        await _send_message_or_edit(query, chat_id, bot, msg, reply_markup=get_main_menu_keyboard())

def _find_and_terminate_processes(process_name_target: str) -> tuple[bool, str]: # Эта функция остается синхронной
    terminated_pids = []
    killed_pids_forcefully = []
    process_name_target_lower = process_name_target.lower()

    for proc in psutil.process_iter(['pid', 'name']):
        try:
            p_name = proc.info['name']
            if p_name and process_name_target_lower in p_name.lower():
                p = psutil.Process(proc.info['pid'])
                p.terminate() # Попытка мягкого завершения
                terminated_pids.append(proc.info['pid'])
                logger.info(f"Запрошено мягкое завершение процесса: {p_name} (PID: {proc.info['pid']})")
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            pass
        except Exception as e:
            logger.error(f"Ошибка при попытке мягкого завершения процесса {proc.info.get('name', 'N/A')}: {e}")

    if not terminated_pids:
        return True, f"Активные процессы, связанные с '{process_name_target}', не найдены для завершения."

    # Даем время на мягкое завершение
    # В асинхронном контексте лучше использовать await asyncio.sleep(1.5)
    # Но т.к. эта функция вызывается из async, но сама по себе блокирующая, time.sleep тут допустим.
    time.sleep(1.5)

    for pid in terminated_pids:
        if psutil.pid_exists(pid): # Проверяем, существует ли еще процесс
            try:
                p = psutil.Process(pid)
                p.kill() # Принудительное завершение
                killed_pids_forcefully.append(pid)
                logger.info(f"Принудительно завершен процесс PID: {pid} (не ответил на terminate)")
            except (psutil.NoSuchProcess, psutil.AccessDenied): # Процесс мог уже завершиться сам
                pass
            except Exception as e:
                logger.error(f"Ошибка при принудительном завершении PID {pid}: {e}")

    msg_parts = [f"Попытка завершения процессов для '{process_name_target}' завершена."]
    if terminated_pids:
         msg_parts.append(f"Запрошено завершение для {len(terminated_pids)} процессов.")
    if killed_pids_forcefully:
        msg_parts.append(f"{len(killed_pids_forcefully)} из них были завершены принудительно.")

    return True, " ".join(msg_parts)


async def _perform_restart_program(program_alias: str, chat_id: int, bot: telegram.Bot, query: telegram.CallbackQuery = None) -> None:
    if program_alias not in ALLOWED_PROGRAMS:
        await _send_message_or_edit(query, chat_id, bot, f"Программа '{program_alias}' не найдена.")
        return

    program_info = ALLOWED_PROGRAMS[program_alias]
    process_to_find_and_kill = program_info.get('process_name')
    display_name = program_info.get("display_name", program_alias)

    if not process_to_find_and_kill:
        msg = f"Для программы '<b>{display_name}</b>' не указано 'process_name' в конфигурации. Не могу перезапустить."
        logger.warning(f"Для {program_alias} не указан 'process_name'.")
        await _send_message_or_edit(query, chat_id, bot, msg, reply_markup=get_main_menu_keyboard())
        return

    await _send_message_or_edit(query, chat_id, bot, f"Начинаю перезапуск '<b>{display_name}</b>'...")
    # await asyncio.sleep(0.5) # Если нужен неблокирующий сон

    # _find_and_terminate_processes синхронная, ее результат получаем сразу
    success_terminate, term_message = _find_and_terminate_processes(process_to_find_and_kill)
    await _send_message_or_edit(query, chat_id, bot, term_message) # Сообщаем о результате завершения
    # await asyncio.sleep(0.5)

    if not success_terminate: # Хотя наша функция всегда возвращает True первым элементом
        await _send_message_or_edit(query, chat_id, bot, f"Не удалось корректно завершить процессы для '<b>{display_name}</b>'. Перезапуск отменен.", reply_markup=get_main_menu_keyboard())
        return

    # Запускаем программу снова (синхронно)
    command_to_run = program_info['cmd']
    try:
        if isinstance(command_to_run, list):
            subprocess.Popen(command_to_run)
        else:
            subprocess.Popen([command_to_run])
        logger.info(f"Команда повторного запуска для '{display_name}' ('{command_to_run}') выполнена.")
        await _send_message_or_edit(query, chat_id, bot, f"Программа '<b>{display_name}</b>' успешно перезапущена.", reply_markup=get_main_menu_keyboard())
    except FileNotFoundError:
        msg = f"Ошибка: Файл программы для '<b>{display_name}</b>' ('{command_to_run}') не найден после перезапуска."
        logger.error(msg)
        await _send_message_or_edit(query, chat_id, bot, msg, reply_markup=get_main_menu_keyboard())
    except Exception as e:
        msg = f"Не удалось перезапустить программу '<b>{display_name}</b>': {e}"
        logger.error(msg)
        await _send_message_or_edit(query, chat_id, bot, msg, reply_markup=get_main_menu_keyboard())


async def _perform_take_screenshot(chat_id: int, bot: telegram.Bot, query: telegram.CallbackQuery = None) -> None:
    try:
        # mss работает синхронно
        with mss.mss() as sct:
            monitor_number = 1 # Первый монитор, если у вас их несколько, может быть другой
            # Проверка, что такой монитор существует. sct.monitors[0] - вся область, sct.monitors[1] - первый физический.
            if len(sct.monitors) <= monitor_number: # Если всего один монитор (или меньше)
                 monitor_info = sct.monitors[0] # Берем всю область (часто это то, что нужно)
            else:
                 monitor_info = sct.monitors[monitor_number] # Берем конкретный монитор

            sct_img = sct.grab(monitor_info)
            mss.tools.to_png(sct_img.rgb, sct_img.size, output=SCREENSHOT_FILENAME)
            logger.info(f"Скриншот сохранен как {SCREENSHOT_FILENAME}")

        with open(SCREENSHOT_FILENAME, 'rb') as photo_file:
            await bot.send_photo(chat_id=chat_id, photo=photo_file, caption="Вот ваш скриншот:")
        logger.info(f"Скриншот отправлен пользователю {chat_id}")
        # После отправки фото, если это был callback, редактируем исходное сообщение
        if query:
            await _send_message_or_edit(query, chat_id, bot, "Выберите следующее действие:", reply_markup=get_main_menu_keyboard())
        else: # Если это был не callback (например, команда /screenshot)
            await bot.send_message(chat_id=chat_id, text="Выберите следующее действие:", reply_markup=get_main_menu_keyboard())


    except Exception as e:
        message = f"Не удалось сделать или отправить скриншот: {e}"
        logger.error(message)
        await _send_message_or_edit(query, chat_id, bot, message, reply_markup=get_main_menu_keyboard())
    finally:
        if os.path.exists(SCREENSHOT_FILENAME):
            try:
                os.remove(SCREENSHOT_FILENAME) # Синхронное удаление
            except Exception as e_rem:
                 logger.error(f"Ошибка при удалении файла {SCREENSHOT_FILENAME}: {e_rem}")

# --- Обработчик нажатий на кнопки ---
async def button_callback_handler(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
    if not await is_user_allowed(update): return

    query = update.callback_query
    await query.answer() # Отвечаем на callback query

    data = query.data
    chat_id = query.message.chat_id
    bot = context.bot # Объект bot доступен через context

    if data == 'menu_main':
        await _send_message_or_edit(query, chat_id, bot, "Главное меню:", reply_markup=get_main_menu_keyboard())
    elif data == 'menu_run':
        await _send_message_or_edit(query, chat_id, bot, "Выберите программу для запуска:", reply_markup=get_programs_keyboard('run'))
    elif data == 'menu_restart':
        await _send_message_or_edit(query, chat_id, bot, "Выберите программу для перезапуска:", reply_markup=get_programs_keyboard('restart'))
    elif data == 'menu_screenshot':
        await _send_message_or_edit(query, chat_id, bot, "Делаю скриншот...")
        await _perform_take_screenshot(chat_id, bot, query)
    elif data.startswith('run:'):
        program_alias = data.split(':', 1)[1]
        display_name = ALLOWED_PROGRAMS.get(program_alias, {}).get('display_name', program_alias)
        await _send_message_or_edit(query, chat_id, bot, f"Запускаю '<b>{display_name}</b>'...")
        await _perform_launch_program(program_alias, chat_id, bot, query)
    elif data.startswith('restart:'):
        program_alias = data.split(':', 1)[1]
        # Сообщение о начале перезапуска будет внутри _perform_restart_program
        await _perform_restart_program(program_alias, chat_id, bot, query)

# --- Обработчик ошибок ---
async def error_handler(update: object, context: ContextTypes.DEFAULT_TYPE) -> None:
    logger.error(msg="Exception while handling an update:", exc_info=context.error)
    # В v20 context.error это само исключение
    # update может быть None, если ошибка произошла вне обработки update
    if isinstance(update, Update) and update.effective_chat:
        try:
            await context.bot.send_message(
                chat_id=update.effective_chat.id,
                text=f"Произошла ошибка. Попробуйте снова или команду /start.\nДетали: {context.error}"
            )
        except Exception as e:
            logger.error(f"Не удалось отправить сообщение об ошибке пользователю: {e}")


def main() -> None:
    if BOT_TOKEN == "ВАШ_ТЕЛЕГРАМ_БОТ_ТОКЕН" or ALLOWED_USER_ID == 123456789:
        print("ПОЖАЛУЙСТА, ОТРЕДАКТИРУЙТЕ СКРИПТ!")
        print("Укажите ваш BOT_TOKEN и ALLOWED_USER_ID в начале файла.")
        return

    # Инициализация для python-telegram-bot v20+
    application = Application.builder().token(BOT_TOKEN).build()

    # Регистрация обработчиков
    application.add_handler(CommandHandler("start", start_command))
    application.add_handler(CallbackQueryHandler(button_callback_handler))
    # Добавьте другие обработчики здесь, если они нужны

    application.add_error_handler(error_handler)

    # Запуск бота
    logger.info(f"Бот запускается для пользователя ID {ALLOWED_USER_ID}...")
    # run_polling() будет работать, пока вы не остановите скрипт (например, Ctrl+C)
    # allowed_updates можно указать, чтобы получать только нужные типы обновлений
    application.run_polling(allowed_updates=Update.ALL_TYPES)
    # logger.info("Бот остановлен.") # Этот лог не будет достигнут при нормальной работе run_polling

if __name__ == '__main__':
    main()
