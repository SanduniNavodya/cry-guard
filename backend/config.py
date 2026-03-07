import os

class Settings:
    APP_NAME: str = "Baby Cry Detection API"
    # Assuming models directory is at the same level as config.py
    BASE_DIR: str = os.path.dirname(os.path.abspath(__file__))
    MODEL_PATH: str = os.path.join(BASE_DIR, "models", "baby_cry_model.h5")
    
    # Audio constants
    SAMPLE_RATE: int = 16000
    MEL_BINS: int = 128
    MAX_FREQ: int = 8000

    # MongoDB
    MONGO_URI: str = os.getenv("MONGO_URI", "mongodb://localhost:27017")
    MONGO_DB_NAME: str = os.getenv("MONGO_DB_NAME", "cryguard")

settings = Settings()
