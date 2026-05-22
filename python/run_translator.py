import argparse
from audio_client import AudioClient


def main():
    parser = argparse.ArgumentParser(description='ESP32 AI Translator Python utility')
    parser.add_argument('--file', help='Path to audio file for offline processing')
    args = parser.parse_args()

    client = AudioClient()
    if args.file:
        client.process_file(args.file)
    else:
        client.record_and_translate()


if __name__ == '__main__':
    main()
