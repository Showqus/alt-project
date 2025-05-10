import logging
import os
import sys
import subprocess
import asyncio
from telegram import Update, InlineKeyboardMarkup, InlineKeyboardButton
from telegram.ext import Application, CommandHandler, CallbackQueryHandler, MessageHandler, filters, ContextTypes
from telegram.constants import ParseMode # Важно для HTML

# --- КОНФИГУРАЦИЯ ---
TELEGRAM_BOT_TOKEN = "5571710712:AAGuAAqCYgiXw8fAg3uaEq-dlNnFCcWFbas" # <--- ЗАМЕНИ НА СВОЙ ТОКЕН
ALLOWED_USER_ID = "1314664622"           # <--- ЗАМЕНИ НА СВОЙ ID (в виде строки)
# --------------------

# Получаем директорию, где находится текущий скрипт
SCRIPT_PATH = os.path.abspath(__file__)
SCRIPT_DIR = os.path.dirname(SCRIPT_PATH)

# Глобальный логгер (будет инициализирован в main)
logger = None

async def start_command(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
    """Отправляет приветственное сообщение при команде /start."""
    user = update.effective_user
    await update.message.reply_html(
        rf"Привет, {user.mention_html()}! Я твой бот. Используй /menu для доступа к командам управления.",
    )
    logger.info(f"Пользователь {user.id} ({user.username}) запустил команду /start")

async def perform_update(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Обновляет бота через git pull."""
    query = update.callback_query
    await query.answer(text="Обновляю бота...")
    await query.edit_message_text(text="⏳ Попытка обновления через `git pull`...")
    logger.info(f"Пользователь {query.from_user.id} инициировал обновление бота.")

    try:
        process = await asyncio.create_subprocess_exec(
            'git', 'pull',
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=SCRIPT_DIR # Указываем рабочую директорию для git
        )
        stdout, stderr = await process.communicate()

        output_message = ""
        if process.returncode == 0:
            git_output = stdout.decode('utf-8', errors='replace').strip()
            if not git_output:
                git_output = "Нет изменений для загрузки."
            elif "Already up to date." in git_output:
                git_output = "Репозиторий уже обновлен до последней версии."

            output_message = f"✅ Обновление завершено!\n\n<b>Результат git pull:</b>\n<pre>{git_output}</pre>\n\nℹ️ Для применения всех изменений рекомендуется перезапустить бота."
            logger.info(f"Git pull successful: {git_output}")
        else:
            git_error_output = stderr.decode('utf-8', errors='replace').strip()
            output_message = f"⚠️ Ошибка при обновлении:\n\n<pre>{git_error_output}</pre>"
            logger.error(f"Git pull failed: {git_error_output}")

        keyboard_after_action = [
            [InlineKeyboardButton("🔁 Перезапустить сейчас", callback_data='restart_bot')],
            [InlineKeyboardButton("Меню", callback_data='show_menu')],
            [InlineKeyboardButton("Закрыть", callback_data='close_menu')]
        ]
        reply_markup_after_action = InlineKeyboardMarkup(keyboard_after_action)
        await query.edit_message_text(text=output_message, reply_markup=reply_markup_after_action, parse_mode=ParseMode.HTML)

    except FileNotFoundError:
        logger.error("Команда 'git' не найдена. Убедитесь, что Git установлен и доступен в PATH.")
        await query.edit_message_text(text="❌ Ошибка: команда 'git' не найдена. Git должен быть установлен и доступен в системном PATH.")
    except Exception as e:
        logger.error(f"Непредвиденная ошибка при обновлении: {e}")
        await query.edit_message_text(text=f"❌ Непредвиденная ошибка при обновлении: {e}")

async def perform_restart(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Перезапускает бота."""
    query = update.callback_query
    await query.answer(text="Перезапускаю бота...")
    logger.info(f"Пользователь {query.from_user.id} инициировал перезапуск бота.")
    try:
        await query.edit_message_text(text="⏳ Бот перезапускается... Пожалуйста, подождите.\nНовое сообщение придет после успешного запуска (если он произойдет).")
    except Exception as e:
        logger.warning(f"Не удалось отредактировать сообщение перед перезапуском: {e}")

    # Небольшая задержка, чтобы успеть отправить сообщение и Telegram его обработал
    await asyncio.sleep(1)

    try:
        logger.info(f"Перезапуск с помощью: {sys.executable} {' '.join(sys.argv)}")
        os.execv(sys.executable, [sys.executable] + sys.argv)
    except Exception as e:
        logger.error(f"Ошибка при попытке перезапуска: {e}")
        # Попытка отправить сообщение об ошибке, если это возможно
        try:
            await context.bot.send_message(chat_id=query.from_user.id, text=f"❌ Критическая ошибка при перезапуске: {e}. Бот может не работать. Проверьте логи.")
        except Exception as send_e:
            logger.error(f"Не удалось отправить сообщение об ошибке перезапуска: {send_e}")


async def show_menu(update: Update, context: ContextTypes.DEFAULT_TYPE, query=None):
    """Отображает главное меню управления или редактирует существующее сообщение."""
    keyboard = [
        [InlineKeyboardButton("🔄 Обновить бота (git pull)", callback_data='update_bot')],
        [InlineKeyboardButton("🔁 Перезапустить бота", callback_data='restart_bot')],
        [InlineKeyboardButton("Закрыть меню", callback_data='close_menu')]
    ]
    reply_markup = InlineKeyboardMarkup(keyboard)
    menu_text = '⚙️ <b>Меню управления ботом:</b>'

    if query: # Если функция вызвана из callback_query (нажатие кнопки "Меню")
        try:
            await query.edit_message_text(text=menu_text, reply_markup=reply_markup, parse_mode=ParseMode.HTML)
        except Exception as e:
            logger.warning(f"Не удалось отредактировать меню (возможно, сообщение было изменено): {e}")
            # Если редактирование не удалось, попробуем отправить новое сообщение
            await context.bot.send_message(chat_id=query.from_user.id, text=menu_text, reply_markup=reply_markup, parse_mode=ParseMode.HTML)

    elif update.message: # Если функция вызвана командой /menu
        await update.message.reply_text(text=menu_text, reply_markup=reply_markup, parse_mode=ParseMode.HTML)


async def menu_command_handler(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
    """Обработчик команды /menu."""
    user_id = update.effective_user.id
    if str(user_id) != ALLOWED_USER_ID:
        await update.message.reply_text("⛔ У вас нет прав для выполнения этой команды.")
        logger.warning(f"Попытка доступа к /menu от неавторизованного пользователя: {user_id} ({update.effective_user.username})")
        return
    await show_menu(update, context)


async def button_callback(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
    """Обрабатывает нажатия на inline-кнопки."""
    query = update.callback_query
    user_id = query.from_user.id

    if str(user_id) != ALLOWED_USER_ID:
        await query.answer("⛔ У вас нет прав для этого действия.", show_alert=True)
        logger.warning(f"Попытка использования кнопки меню от неавторизованного пользователя: {user_id}, data: {query.data}")
        return

    data = query.data
    logger.info(f"Нажата кнопка: {data} пользователем {user_id}")

    if data == 'update_bot':
        await perform_update(update, context)
    elif data == 'restart_bot':
        await perform_restart(update, context)
    elif data == 'show_menu':
        await query.answer() # Отвечаем на callback перед редактированием
        await show_menu(update, context, query=query)
    elif data == 'close_menu':
        await query.answer()
        try:
            await query.edit_message_text(text="Меню закрыто.")
        except Exception as e:
            logger.warning(f"Не удалось закрыть меню (возможно, оно уже было изменено): {e}")
    else:
        await query.answer("Неизвестное действие.") # Отвечаем на callback
        logger.warning(f"Получен неизвестный callback_data: {data}")

async def post_init(application: Application) -> None:
    """Выполняется после инициализации приложения и перед запуском поллинга."""
    if ALLOWED_USER_ID:
        try:
            await application.bot.send_message(
                chat_id=ALLOWED_USER_ID,
                text="✅ Бот успешно запущен/перезапущен и готов к работе!\nИспользуйте /menu для управления."
            )
            logger.info(f"Отправлено уведомление о запуске пользователю {ALLOWED_USER_ID}")
        except Exception as e:
            logger.error(f"Не удалось отправить уведомление о запуске пользователю {ALLOWED_USER_ID}: {e}")

def main() -> None:
    """Основная функция запуска бота."""
    global logger # Делаем logger доступным глобально

    # Настройка логирования
    # Установка PYTHONIOENCODING=UTF-8 в переменных окружения рекомендуется для Windows
    # или chcp 65001 в CMD перед запуском
    logging.basicConfig(
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        level=logging.INFO,
        handlers=[
            logging.FileHandler("bot.log", encoding='utf-8'), # Логирование в файл
            logging.StreamHandler(sys.stdout) # Логирование в консоль (убедитесь, что консоль поддерживает UTF-8)
        ]
    )
    logger = logging.getLogger(__name__) # Инициализируем глобальный логгер

    if not TELEGRAM_BOT_TOKEN or TELEGRAM_BOT_TOKEN == "ТВОЙ_ТЕЛЕГРАМ_БОТ_ТОКЕН":
        logger.critical("TELEGRAM_BOT_TOKEN не задан или используется значение по умолчанию! Бот не может быть запущен.")
        sys.exit("Ошибка: TELEGRAM_BOT_TOKEN не задан. Укажите токен в коде.")

    if not ALLOWED_USER_ID or ALLOWED_USER_ID == "ТВОЙ_ТЕЛЕГРАМ_ID":
        logger.critical("ALLOWED_USER_ID не задан или используется значение по умолчанию! Функции управления будут недоступны или доступны всем.")
        # Можно либо завершить работу, либо продолжить с предупреждением.
        # Для безопасности лучше завершить, если этот ID критичен для управления.
        # sys.exit("Ошибка: ALLOWED_USER_ID не задан. Укажите ваш Telegram ID.")
        logger.warning("ALLOWED_USER_ID не задан! Команды управления могут быть недоступны или небезопасны.")


    logger.info(f"Запуск бота. SCRIPT_PATH: {SCRIPT_PATH}, SCRIPT_DIR: {SCRIPT_DIR}")
    logger.info(f"Разрешенный пользователь ID: {ALLOWED_USER_ID}")

    # Создание Application
    application = Application.builder().token(TELEGRAM_BOT_TOKEN).post_init(post_init).build()

    # Регистрация обработчиков
    application.add_handler(CommandHandler("start", start_command))
    application.add_handler(CommandHandler("menu", menu_command_handler))
    application.add_handler(CallbackQueryHandler(button_callback))

    # Тут могут быть другие ваши обработчики сообщений, если они есть
    # application.add_handler(MessageHandler(filters.TEXT & ~filters.COMMAND, echo_message_handler))

    # Запуск бота
    logger.info(f"Бот запускается и ожидает команд...")
    try:
        application.run_polling()
    except Exception as e:
        logger.critical(f"Критическая ошибка при запуске или работе application.run_polling(): {e}", exc_info=True)
    finally:
        logger.info("Бот остановлен.")


if __name__ == '__main__':
    # Для Windows PowerShell рекомендуется установить кодировку перед запуском, если возникают проблемы с Unicode в консоли:
    # $env:PYTHONIOENCODING="UTF-8"
    # python alt.py
    #
    # Для CMD:
    # chcp 65001
    # set PYTHONIOENCODING=UTF-8
    # python alt.py
    main()
