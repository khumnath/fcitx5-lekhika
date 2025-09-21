#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
lekhika_cli.py - A simple command-line tool to manage the fcitx5-lekhika dictionary.

This script provides a CLI interface to perform the main functionalities of the
Lekhika dictionary trainer, such as learning from a text file, adding or removing
words, and viewing database information.

It interacts with the same SQLite database used by the GUI application and
is optimized to handle very large files efficiently.

Dependencies:
This script uses only Python's standard libraries. No external packages are needed.
It is good practice to run Python scripts in a virtual environment.

To set up a virtual environment:
1. Create the environment:
   python3 -m venv .venv
2. Activate it:
   source .venv/bin/activate
   (On Windows, use: .venv\\Scripts\\activate)
"""

import sqlite3
import argparse
import re
from pathlib import Path
import sys
import os
import multiprocessing

# --- Constants and Configuration ---

# Define the path to the database, ensuring it's the same as the C++ app.
DB_DIR = Path.home() / ".local" / "share" / "fcitx5-lekhika"
DB_PATH = DB_DIR / "lekhikadict.akshardb"
CHUNK_SIZE = 15 * 1024 * 1024  # 15 MB chunks for learning from files

# Devanagari Unicode character ranges for validation
DEVANAGARI_BLOCK = (0x0900, 0x097F)
INDEPENDENT_VOWELS = (0x0904, 0x0914)
DEPENDENT_VOWEL_SIGNS = (0x093E, 0x094C)
HALANT = 0x094D
OM = 0x0950
CONSONANTS = (0x0915, 0x0939)

def is_valid_devanagari_word(word: str) -> bool:
    """
    Validates if a string is a legitimate Devanagari word based on phonetic rules.
    This is a Python port of the core logic from the C++ validator.

    Args:
        word: The string to validate.

    Returns:
        True if the word is valid, False otherwise.
    """
    if not word or len(word) < 2:
        return False

    codepoints = [ord(c) for c in word]

    # Rule 1: All characters must be within the Devanagari block.
    for cp in codepoints:
        if not (DEVANAGARI_BLOCK[0] <= cp <= DEVANAGARI_BLOCK[1]):
            return False

    # Rule 2: Cannot end with a Halant (Virama).
    if codepoints[-1] == HALANT:
        return False

    # Rule 3: The first character must be a consonant, independent vowel, or Om.
    first_char_cp = codepoints[0]
    is_first_consonant = CONSONANTS[0] <= first_char_cp <= CONSONANTS[1]
    is_first_independent_vowel = INDEPENDENT_VOWELS[0] <= first_char_cp <= INDEPENDENT_VOWELS[1]
    if not (is_first_consonant or is_first_independent_vowel or first_char_cp == OM):
        return False

    # Rule 4: Check for invalid sequences within the word.
    for i, cp in enumerate(codepoints):
        # Independent vowels are only allowed as the very first character.
        # This implicitly handles invalid sequences like Halant + Independent Vowel.
        if i > 0:
            is_independent_vowel = INDEPENDENT_VOWELS[0] <= cp <= INDEPENDENT_VOWELS[1]
            if is_independent_vowel:
                return False

    return True

def validate_word_list(words):
    """Helper function for multiprocessing to validate a list of words."""
    return [word for word in words if is_valid_devanagari_word(word)]


class DictionaryManager:
    """A class to manage all interactions with the SQLite database."""

    def __init__(self, db_path: Path):
        """Initializes the manager and connects to the database."""
        db_path.parent.mkdir(parents=True, exist_ok=True)
        self.db_path = db_path
        self.conn = sqlite3.connect(str(db_path))
        self.create_tables_if_not_exists()

    def create_tables_if_not_exists(self):
        """Creates the database schema if it doesn't already exist."""
        sql_script = """
            CREATE TABLE IF NOT EXISTS words (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                word TEXT NOT NULL UNIQUE,
                frequency INTEGER NOT NULL DEFAULT 1
            );
            CREATE INDEX IF NOT EXISTS idx_word ON words(word);
            CREATE TABLE IF NOT EXISTS meta (
                key TEXT PRIMARY KEY,
                value TEXT
            );
            INSERT OR IGNORE INTO meta (key, value) VALUES ('format_version', '1.0');
            INSERT OR IGNORE INTO meta (key, value) VALUES ('engine', 'lekhika');
            INSERT OR IGNORE INTO meta (key, value) VALUES ('language', 'ne');
            INSERT OR IGNORE INTO meta (key, value) VALUES ('script', 'Devanagari');
            INSERT OR IGNORE INTO meta (key, value) VALUES ('created_at', datetime('now'));
        """
        with self.conn:
            self.conn.executescript(sql_script)

    def learn_from_file(self, file_path: str):
        """
        Reads a large text file in chunks, validates words in parallel, and adds to the DB.
        """
        try:
            file_size = os.path.getsize(file_path)
            num_cpus = multiprocessing.cpu_count()
            total_chunks = (file_size + CHUNK_SIZE - 1) // CHUNK_SIZE if file_size > 0 else 1
            print(f"Starting job...\n"
                  f"  - File: {file_path}\n"
                  f"  - Size: {file_size / (1024*1024):.2f} MB\n"
                  f"  - Chunks: {total_chunks} (up to {CHUNK_SIZE / (1024*1024):.2f} MB each)\n"
                  f"  - CPU Cores: {num_cpus}")
        except OSError as e:
            print(f"Error: Could not access file. {e}", file=sys.stderr)
            return

        all_valid_words = []
        leftover = ""
        
        with open(file_path, 'r', encoding='utf-8') as f:
            for i in range(total_chunks):
                print(f"\n--- Processing chunk {i+1} of {total_chunks} ---")
                content = leftover + f.read(CHUNK_SIZE)
                
                if not content:
                    continue

                if i < total_chunks - 1:
                    split_point = content.rfind(' ')
                    if split_point != -1:
                        leftover = content[split_point:]
                        content = content[:split_point]
                    else:
                        leftover = ""

                print("Finding potential words...")
                potential_words = re.findall(r'[\u0900-\u097F]+', content)
                
                print(f"Found {len(potential_words)} potential words. Validating in parallel...")
                
                with multiprocessing.Pool(processes=num_cpus) as pool:
                    chunk_size_for_pool = (len(potential_words) + num_cpus - 1) // num_cpus
                    word_chunks = [potential_words[j:j + chunk_size_for_pool] for j in range(0, len(potential_words), chunk_size_for_pool)]
                    
                    results = pool.map(validate_word_list, word_chunks)
                
                chunk_valid_words = [word for sublist in results for word in sublist]
                all_valid_words.extend(chunk_valid_words)
                print(f"Found {len(chunk_valid_words)} valid words in this chunk.")

        if not all_valid_words:
            print("\nNo valid Devanagari words found to add.")
            return

        print(f"\n--- Database Update ---")
        print(f"Adding {len(all_valid_words)} total valid words to the database...")
        sql = "INSERT INTO words (word) VALUES (?) ON CONFLICT(word) DO UPDATE SET frequency = frequency + 1;"
        
        try:
            with self.conn:
                self.conn.execute("BEGIN TRANSACTION;")
                self.conn.executemany(sql, [(word,) for word in all_valid_words])
                self.conn.execute("COMMIT;")
            print("Successfully learned from file and updated the dictionary.")
        except sqlite3.Error as e:
            print(f"Database error during bulk insert: {e}", file=sys.stderr)

    def add_word(self, word: str):
        """Adds a single word to the database or increments its frequency."""
        if not is_valid_devanagari_word(word):
            print(f"Error: '{word}' is not a valid Devanagari word.", file=sys.stderr)
            return
        sql = "INSERT INTO words (word) VALUES (?) ON CONFLICT(word) DO UPDATE SET frequency = frequency + 1;"
        with self.conn:
            self.conn.execute(sql, (word,))
        print(f"Successfully added or updated '{word}'.")

    def remove_word(self, word: str):
        """Removes a single word from the database."""
        sql = "DELETE FROM words WHERE word = ?;"
        with self.conn:
            cursor = self.conn.execute(sql, (word,))
            if cursor.rowcount > 0:
                print(f"Successfully removed '{word}'.")
            else:
                print(f"Word '{word}' not found in the dictionary.")

    def list_words(self, limit: int = 50, sort_by: str = 'word'):
        """Lists words from the database."""
        order_clause = "frequency DESC" if sort_by == 'freq' else "word ASC"
        sql = f"SELECT word, frequency FROM words ORDER BY {order_clause} LIMIT ?;"
        cursor = self.conn.execute(sql, (limit,))
        rows = cursor.fetchall()
        if not rows:
            print("The dictionary is empty.")
            return
        
        print(f"{'Word':<20} | {'Frequency'}")
        print("-" * 32)
        for word, frequency in rows:
            print(f"{word:<20} | {frequency}")

    def get_info(self):
        """Prints metadata and statistics about the database."""
        print("--- Lekhika Database Info ---")
        print(f"Location: {self.db_path}")
        
        meta_sql = "SELECT key, value FROM meta;"
        cursor = self.conn.execute(meta_sql)
        for key, value in cursor.fetchall():
            print(f"- {key.replace('_', ' ').capitalize()}: {value}")
            
        count_sql = "SELECT COUNT(*) FROM words;"
        cursor = self.conn.execute(count_sql)
        word_count = cursor.fetchone()[0]
        print(f"- Total words: {word_count}")
        print("-" * 29)

    def reset_database(self):
        """Deletes all words from the dictionary."""
        sql = "DELETE FROM words;"
        with self.conn:
            self.conn.execute(sql)
        print("Dictionary has been reset. All words have been deleted.")

    def close(self):
        """Closes the database connection."""
        self.conn.close()


def main():
    """Main function to parse arguments and execute commands."""
    parser = argparse.ArgumentParser(
        description="A CLI tool to manage the fcitx5-lekhika dictionary.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    subparsers = parser.add_subparsers(dest="command", required=True, help="Available commands")

    # --- 'learn' command ---
    parser_learn = subparsers.add_parser("learn", help="Learn Devanagari words from a text file.")
    parser_learn.add_argument("file_path", type=str, help="Path to the UTF-8 encoded text file.")

    # --- 'add' command ---
    parser_add = subparsers.add_parser("add", help="Add a single word to the dictionary.")
    parser_add.add_argument("word", type=str, help="The Devanagari word to add.")

    # --- 'remove' command ---
    parser_remove = subparsers.add_parser("remove", help="Remove a single word from the dictionary.")
    parser_remove.add_argument("word", type=str, help="The Devanagari word to remove.")

    # --- 'list' command ---
    parser_list = subparsers.add_parser("list", help="List words in the dictionary.")
    parser_list.add_argument("--limit", type=int, default=50, help="Number of words to show (default: 50).")
    parser_list.add_argument("--sort", type=str, choices=['word', 'freq'], default='word', help="Sort by 'word' or 'freq' (frequency).")
    
    # --- 'info' command ---
    subparsers.add_parser("info", help="Show information about the database.")
    
    # --- 'reset' command ---
    parser_reset = subparsers.add_parser("reset", help="DANGER: Deletes all words from the dictionary.")
    parser_reset.add_argument("--i-am-sure", action="store_true", help="Confirmation flag required to reset the DB.")

    args = parser.parse_args()
    
    if not DB_PATH.exists() and args.command != 'learn':
        print("Database not found. Learn from a file first to create it.", file=sys.stderr)
        # Show help if the DB doesn't exist for any command except 'learn'.
        if not os.path.exists(DB_DIR):
             os.makedirs(DB_DIR)
        
    db_manager = DictionaryManager(DB_PATH)

    try:
        if args.command == "learn":
            db_manager.learn_from_file(args.file_path)
        elif args.command == "add":
            db_manager.add_word(args.word)
        elif args.command == "remove":
            db_manager.remove_word(args.word)
        elif args.command == "list":
            db_manager.list_words(args.limit, args.sort)
        elif args.command == "info":
            db_manager.get_info()
        elif args.command == "reset":
            if args.i_am_sure:
                db_manager.reset_database()
            else:
                print("Error: To reset the database, you must provide the --i-am-sure flag.", file=sys.stderr)
    finally:
        db_manager.close()

if __name__ == "__main__":
    main()

