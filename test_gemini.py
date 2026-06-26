import os
from dotenv import load_dotenv
import google.generativeai as genai

load_dotenv()

API_KEY = os.getenv("GEMINI_API_KEY")
if not API_KEY:
    print("❌ GEMINI_API_KEY not found in .env")
    exit(1)

genai.configure(api_key=API_KEY)

models_to_test = ["gemini-2.5-flash", "gemini-2.0-flash", "gemini-1.5-flash", "gemini-2.5-pro", "gemini-1.5-pro"]
working_model = None

print("Testing Gemini AI Connection...")
for model_name in models_to_test:
    print(f"\nTrying model: {model_name}...")
    try:
        model = genai.GenerativeModel(model_name)
        response = model.generate_content("Hello, this is a connection test. Reply with 'Connection successful'.")
        print(f"✅ Success! Response: {response.text.strip()}")
        working_model = model_name
        break
    except Exception as e:
        print(f"❌ Failed: {e}")

if working_model:
    print(f"\n🎉 Best working model: {working_model}")
else:
    print("\n🚨 All models failed.")
