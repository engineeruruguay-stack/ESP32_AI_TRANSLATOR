import sounddevice as sd
import numpy as np

class AudioClient:
    def __init__(self, sample_rate=16000, channels=1):
        self.sample_rate = sample_rate
        self.channels = channels

    def record_chunk(self, duration=3):
        print(f"Запись {duration} секунд аудио...")
        recording = sd.rec(int(duration * self.sample_rate), samplerate=self.sample_rate, channels=self.channels, dtype='int16')
        sd.wait()
        return np.squeeze(recording)

    def process_file(self, path):
        print(f"Обработка файла: {path}")
        # TODO: реализовать загрузку файла и отправку на модель.

    def record_and_translate(self):
        samples = self.record_chunk()
        print(f"Получено {len(samples)} аудио-сэмплов")
        text = self.recognize_speech(samples)
        if text:
            translation = self.translate_text(text)
            print(f"Распознано: {text}")
            print(f"Перевод: {translation}")

    def recognize_speech(self, samples):
        # TODO: интегрировать локальную или удаленную модель распознавания речи.
        return "hola mundo"

    def translate_text(self, spanish_text):
        # TODO: интегрировать модель перевода.
        return "привет мир"
