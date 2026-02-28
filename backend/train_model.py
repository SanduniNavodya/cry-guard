import os
import glob
import numpy as np
import tensorflow as tf
from sklearn.model_selection import train_test_split
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Conv2D, MaxPooling2D, Flatten, Dense, Dropout

from config import settings
from utils.audio_processing import preprocess_audio

def load_data(data_dir="data"):
    """
    Load and preprocess all audio files in the data directory.
    Assumes structure:
    data/
      crying/
        audio1.wav
      not_crying/
        audio2.wav
    """
    X = []
    y = []

    crying_path = os.path.join(data_dir, "crying", "*.wav")
    not_crying_path = os.path.join(data_dir, "not_crying", "*.wav")

    # Load crying data (Label: 1)
    for file in glob.glob(crying_path):
        try:
            with open(file, "rb") as f:
                features = preprocess_audio(f.read())
                # preprocess_audio returns shape (1, height, width, 1), we need just (height, width, 1) per sample
                X.append(features[0])
                y.append(1)
        except Exception as e:
            print(f"Error processing {file}: {e}")

    # Load non-crying data (Label: 0)
    for file in glob.glob(not_crying_path):
        try:
            with open(file, "rb") as f:
                features = preprocess_audio(f.read())
                X.append(features[0])
                y.append(0)
        except Exception as e:
            print(f"Error processing {file}: {e}")

    return np.array(X), np.array(y)

def build_model(input_shape):
    """
    Build a simple Convolutional Neural Network for audio classification.
    """
    model = Sequential([
        Conv2D(32, (3, 3), activation='relu', input_shape=input_shape),
        MaxPooling2D((2, 2)),
        Dropout(0.25),
        
        Conv2D(64, (3, 3), activation='relu'),
        MaxPooling2D((2, 2)),
        Dropout(0.25),
        
        Flatten(),
        Dense(64, activation='relu'),
        Dropout(0.5),
        Dense(1, activation='sigmoid') # Binary classification (Crying vs Not Crying)
    ])
    
    model.compile(optimizer='adam',
                  loss='binary_crossentropy',
                  metrics=['accuracy'])
    return model

if __name__ == "__main__":
    print("Loading data from 'data/' directory...")
    X, y = load_data()

    if len(X) == 0:
        print("No training data found. Please add WAV files to data/crying and data/not_crying folders.")
        exit(1)

    print(f"Loaded {len(X)} samples.")
    
    # Split the data into training and testing sets
    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)

    input_shape = X_train.shape[1:] # e.g., (128, X, 1)
    
    model = build_model(input_shape)
    model.summary()

    print("Training model...")
    # Using a small number of epochs. Increase this for real training (e.g. 50-100)
    history = model.fit(
        X_train, y_train,
        epochs=15, 
        batch_size=8,
        validation_data=(X_test, y_test)
    )

    # Save the model
    os.makedirs(os.path.dirname(settings.MODEL_PATH), exist_ok=True)
    model.save(settings.MODEL_PATH)
    print(f"Model saved to {settings.MODEL_PATH}!")
