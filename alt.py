import logging
import os
import sys
import subprocess
import asyncio
import html # Для экранирования HTML

from telegram import Update
from telegram.ext import Application, CommandHandler, ContextTypes # Убраны CallbackQueryHandler, InlineKeyboardMarkup, InlineKeyboardButton
from telegram.constants import ParseMode

# --- КОНФИГУРАЦИЯ ---
TELEGRAM_BOT_TOKEN = "ТВОЙ_ТЕЛЕГРАМ_БОТ_ТОКЕН" # <--- ЗАМЕНИ НА СВОЙ ТОКЕН
ALLOWED_USER_ID = "ТВОЙ_ТЕЛЕГРАМ_ID"           # <--- ЗАМЕНИ НА СВОЙ ID (в виде строки)
# --------------------

SCRIPT_PATH = os.path.abspath(__file__)
SCRIPT_DIR = os.path.dirname(SCRIPT_PATH)
logger = None

async def start_command(update: Update, context: ContextTypes.DEFAULT_TYPE) -> None:
    """Отправляет приветственное сообщение при команде /start."""
    user = update.effective_user
    await update.message.reply_html(
        rf"Привет, {user.mention_html()}! Я твой бот. Список доступных команд можно посмотреть через кнопку 'Меню'.",
    )
    logger.info(f"Пользователь {user.id} ({user.username}) запустил команду /start")

async def update_command_handler(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Обновляет бота через git pull (команда /update)."""
    user_id = update.effective_user.id
    if str(user_id) != ALLOWED_USER_ID:
        await update.message.reply_text("⛔ У вас нет прав для выполнения этой команды.")
        logger.warning(f"Попытка доступа к /update от неавторизованного пользователя: {user_id} ({update.effective_user.username})")
        return

    await update.message.reply_text("⏳ Попытка обновления через <code>git pull</code>...", parse_mode=ParseMode.HTML)
    logger.info(f"Пользователь {user_id} инициировал обновление бота через /update.")

    try:
        process = await asyncio.create_subprocess_exec(
            'git', 'pull',
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=SCRIPT_DIR
        )
        stdout, stderr = await process.communicate()

        output_message = ""
        if process.returncode == 0:
            git_output_raw = stdout.decode('utf-8', errors='replace').strip()
            if not git_output_raw:
                git_output_raw = "Нет изменений для загрузки."
            elif "Already up to date." in git_output_raw:
                git_output_raw = "Репозиторий уже обновлен до последней версии."

            escaped_git_output = html.escape(git_output_raw)
            output_message = f"✅ Обновление завершено!\n\n<b>Результат git pull:</b>\n<pre>{escaped_git_output}</pre>\n\nℹ️ Для применения всех изменений используйте команду /restart."
            logger.info(f"Git pull successful (raw): {git_output_raw}")
        else:
            git_error_output_raw = stderr.decode('utf-8', errors='replace').strip()
            escaped_git_error_output = html.escape(git_error_output_raw)
            output_message = f"⚠️ Ошибка при обновлении:\n\n<pre>{escaped_git_error_output}</pre>"
            logger.error(f"Git pull failed (raw): {git_error_output_raw}")

        await update.message.reply_html(output_message) # Отправляем результат как HTML

    except FileNotFoundError:
        logger.error("Команда 'git' не найдена. Убедитесь, что Git установлен и доступен в PATH.")
        await update.message.reply_text("❌ Ошибка: команда <code>git</code> не найдена. Git должен быть установлен и доступен в системном PATH.", parse_mode=ParseMode.HTML)
    except Exception as e:
        logger.error(f"Непредвиденная ошибка при обновлении: {e}", exc_info=True)
        escaped_error_message = html.escape(str(e))
        await update.message.reply_text(f"❌ Непредвиденная ошибка при обновлении: <pre>{escaped_error_message}</pre>", parse_mode=ParseMode.HTML)

async def restart_command_handler(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Перезапускает бота (команда /restart)."""
    user_id = update.effective_user.id
    if str(user_id) != ALLOWED_USER_ID:
        await update.message.reply_text("⛔ У вас нет прав для выполнения этой команды.")
        logger.warning(f"Попытка доступа к /restart от неавторизованного пользователя: {user_id} ({update.effective_user.username})")
        return

    logger.info(f"Пользователь {user_id} инициировал перезапуск бота через /restart.")
    try:
        await update.message.reply_text("⏳ Бот перезапускается... Пожалуйста, подождите.\nНовое сообщение придет после успешного запуска (если он произойдет).", parse_mode=ParseMode.HTML)
    except Exception as e:
        logger.warning(f"Не удалось отправить сообщение перед перезапуском: {e}")

    await asyncio.sleep(1)

    try:
        logger.info(f"Перезапуск с помощью: {sys.executable} {' '.join(sys.argv)}")
        os.execv(sys.executable, [sys.executable] + sys.argv)
    except Exception as e:
        logger.error(f"Ошибка при попытке перезапуска: {e}", exc_info=True)
        try:
            escaped_error_message = html.escape(str(e))
            # Если бот уже в процессе падения, это сообщение может не отправиться,
            # но мы попытаемся.
            await context.bot.send_message(chat_id=user_id, text=f"❌ Критическая ошибка при перезапуске: <pre>{escaped_error_message}</pre>. Бот может не работать. Проверьте логи.", parse_mode=ParseMode.HTML)
        except Exception as send_e:
            logger.error(f"Не удалось отправить сообщение об ошибке перезапуска: {send_e}")

async def post_init(application: Application) -> None:
    """Выполняется после инициализации приложения и перед запуском поллинга."""
    if ALLOWED_USER_ID and ALLOWED_USER_ID != "ТВОЙ_ТЕЛЕГРАМ_ID":
        try:
            await application.bot.send_message(
                chat_id=ALLOWED_USER_ID,
                text="✅ Бот успешно запущен/перезапущен и готов к работе!\nИспользуйте кнопку 'Меню' для просмотра доступных команд.",
                parse_mode=ParseMode.HTML
            )
            logger.info(f"Отправлено уведомление о запуске пользователю {ALLOWED_USER_ID}")
        except Exception as e:
            logger.error(f"Не удалось отправить уведомление о запуске пользователю {ALLOWED_USER_ID}: {e}")

def main() -> None:
    global logger
    logging.basicConfig(
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        level=logging.INFO,
        handlers=[
            logging.FileHandler("bot.log", encoding='utf-8'),
            logging.StreamHandler(sys.stdout)
        ]
    )
    logger = logging.getLogger(__name__)

    if not TELEGRAM_BOT_TOKEN or TELEGRAM_BOT_TOKEN == "ТВОЙ_ТЕЛЕГРАМ_БОТ_ТОКЕН":
        logger.critical("TELEGRAM_BOT_TOKEN не задан!")
        sys.exit("Ошибка: TELEGRAM_BOT_TOKEN не задан.")

    if not ALLOWED_USER_ID or ALLOWED_USER_ID == "ТВОЙ_ТЕЛЕГРАМ_ID":
        logger.critical("ALLOWED_USER_ID не задан!")
        # sys.exit("Ошибка: ALLOWED_USER_ID не задан.") # Раскомментируйте для принудительного выхода
        logger.warning("ALLOWED_USER_ID не задан! Команды управления могут быть небезопасны.")

    logger.info(f"Запуск бота. SCRIPT_PATH: {SCRIPT_PATH}, SCRIPT_DIR: {SCRIPT_DIR}")
    logger.info(f"Разрешенный пользователь ID: {ALLOWED_USER_ID}")

    application = Application.builder().token(TELEGRAM_BOT_TOKEN).post_init(post_init).build()

    # Регистрация обработчиков команд
    application.add_handler(CommandHandler("start", start_command))
    application.add_handler(CommandHandler("update", update_command_handler))
    application.add_handler(CommandHandler("restart", restart_command_handler))
    # Убраны обработчики menu_command_handler и button_callback

    logger.info(f"Бот запускается и ожидает команд...")
    try:
        application.run_polling()
    except Exception as e:
        logger.critical(f"Критическая ошибка при запуске или работе application.run_polling(): {e}", exc_info=True)
    finally:
        logger.info("Бот остановлен.")

if __name__ == '__main__':
    main()
