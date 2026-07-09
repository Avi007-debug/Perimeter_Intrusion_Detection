import os
import telebot
from dotenv import load_dotenv

load_dotenv()

bot = telebot.TeleBot(os.getenv("TELEGRAM_BOT_TOKEN"))
chat_id = os.getenv("TELEGRAM_CHAT_ID")

try:
    print(f"Testing sending message to {chat_id}...")
    bot.send_message(chat_id, "🔔 *Test Alert* from backend script! If you see this, the bot works.", parse_mode="Markdown")
    print("✅ Success!")
except Exception as e:
    print(f"❌ Failed: {e}")
