#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys
from typing import Iterable


@dataclass(frozen=True)
class Token:
    text: str
    start: int
    end: int
    line: int


def _line_start(text: str, offset: int) -> int:
    return text.rfind("\n", 0, offset) + 1


def _indent_at(text: str, offset: int) -> str:
    start = _line_start(text, offset)
    index = start

    while index < len(text) and text[index] in " \t":
        index += 1

    return text[start:index]


def tokenize_c(text: str) -> list[Token]:
    """
    Tokenize enough C syntax to locate braced if/else blocks.

    Comments, strings, character literals, and preprocessor directives are
    skipped so their braces and keywords are not modified.
    """
    tokens: list[Token] = []
    index = 0
    line = 0
    length = len(text)
    at_line_start = True

    while index < length:
        char = text[index]

        if char == "\n":
            line += 1
            index += 1
            at_line_start = True
            continue

        if char in " \t\r\f\v":
            index += 1
            continue

        # Ignore preprocessor directives, including continued lines.
        if at_line_start and char == "#":
            while index < length:
                if text[index] == "\n":
                    previous = index - 1

                    while previous >= 0 and text[previous] == "\r":
                        previous -= 1

                    if previous >= 0 and text[previous] == "\\":
                        line += 1
                        index += 1
                        continue

                    line += 1
                    index += 1
                    at_line_start = True
                    break

                index += 1

            continue

        at_line_start = False

        if text.startswith("//", index):
            newline = text.find("\n", index + 2)

            if newline == -1:
                break

            index = newline
            continue

        if text.startswith("/*", index):
            end = text.find("*/", index + 2)

            if end == -1:
                line += text.count("\n", index)
                break

            line += text.count("\n", index, end + 2)
            index = end + 2
            continue

        if char in {'"', "'"}:
            quote = char
            index += 1

            while index < length:
                if text[index] == "\\":
                    index += 2
                    continue

                if text[index] == quote:
                    index += 1
                    break

                if text[index] == "\n":
                    line += 1

                index += 1

            continue

        if char == "_" or char.isalpha():
            start = index
            index += 1

            while index < length and (
                text[index] == "_" or text[index].isalnum()
            ):
                index += 1

            tokens.append(Token(text[start:index], start, index, line))
            continue

        tokens.append(Token(char, index, index + 1, line))
        index += 1

    return tokens


def matching_pairs(
    tokens: list[Token],
    opening: str,
    closing: str,
) -> dict[int, int]:
    stack: list[int] = []
    pairs: dict[int, int] = {}

    for index, token in enumerate(tokens):
        if token.text == opening:
            stack.append(index)
        elif token.text == closing and stack:
            start = stack.pop()
            pairs[start] = index

    return pairs


def split_joined_else(text: str) -> str:
    """
    Convert:

        } else {

    into:

        }

        else {

    without touching comments, strings, or preprocessor directives.
    """
    tokens = tokenize_c(text)
    replacements: list[tuple[int, int, str]] = []

    for index, token in enumerate(tokens):
        if token.text != "else" or index == 0:
            continue

        previous = tokens[index - 1]

        if previous.text != "}" or previous.line != token.line:
            continue

        indent = _indent_at(text, previous.start)
        replacements.append(
            (previous.end, token.start, "\n\n" + indent)
        )

    for start, end, replacement in reversed(replacements):
        text = text[:start] + replacement + text[end:]

    return text


def find_if_else_blocks(text: str) -> list[tuple[int, int, int]]:
    """
    Return tuples containing:

        statement line, opening-brace line, closing-brace line
    """
    tokens = tokenize_c(text)
    parentheses = matching_pairs(tokens, "(", ")")
    braces = matching_pairs(tokens, "{", "}")
    blocks: list[tuple[int, int, int]] = []

    def add_if_block(start_index: int, if_index: int) -> None:
        open_parenthesis = if_index + 1

        if (
            open_parenthesis >= len(tokens)
            or tokens[open_parenthesis].text != "("
        ):
            return

        close_parenthesis = parentheses.get(open_parenthesis)

        if close_parenthesis is None:
            return

        open_brace = close_parenthesis + 1

        if open_brace >= len(tokens) or tokens[open_brace].text != "{":
            return

        close_brace = braces.get(open_brace)

        if close_brace is None:
            return

        blocks.append(
            (
                tokens[start_index].line,
                tokens[open_brace].line,
                tokens[close_brace].line,
            )
        )

    for index, token in enumerate(tokens):
        if token.text == "if":
            # The preceding else token handles an else-if chain.
            if index > 0 and tokens[index - 1].text == "else":
                continue

            add_if_block(index, index)
            continue

        if token.text != "else":
            continue

        following = index + 1

        if following >= len(tokens):
            continue

        if tokens[following].text == "if":
            add_if_block(index, following)
            continue

        if tokens[following].text != "{":
            continue

        close_brace = braces.get(following)

        if close_brace is None:
            continue

        blocks.append(
            (
                token.line,
                tokens[following].line,
                tokens[close_brace].line,
            )
        )

    return blocks


def format_text(text: str) -> str:
    newline = "\r\n" if "\r\n" in text else "\n"
    had_final_newline = text.endswith(("\n", "\r"))

    # Work internally with Unix newlines and restore the original convention.
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = split_joined_else(text)

    before_lines: set[int] = set()
    after_lines: set[int] = set()

    for statement_line, opening_line, closing_line in find_if_else_blocks(text):
        before_lines.add(statement_line)
        after_lines.add(opening_line)
        before_lines.add(closing_line)

    lines = text.split("\n")
    output: list[str] = []

    def append_blank() -> None:
        if output and output[-1].strip() != "":
            output.append("")

    for index, line in enumerate(lines):
        if index in before_lines:
            append_blank()

        output.append(line)

        if index in after_lines:
            next_is_blank = (
                index + 1 < len(lines)
                and lines[index + 1].strip() == ""
            )

            if not next_is_blank:
                output.append("")

    result = "\n".join(output)

    if had_final_newline:
        result = result.rstrip("\n") + "\n"
    else:
        result = result.rstrip("\n")

    if newline != "\n":
        result = result.replace("\n", newline)

    return result


def iter_source_files(paths: Iterable[str]) -> list[Path]:
    result: list[Path] = []
    seen: set[Path] = set()

    for raw_path in paths:
        path = Path(raw_path)

        if path.is_dir():
            candidates = (
                candidate
                for candidate in path.rglob("*")
                if candidate.is_file()
                and candidate.suffix.lower() in {".c", ".h"}
            )
        elif path.is_file():
            candidates = iter((path,))
        else:
            print(f"warning: not found: {path}", file=sys.stderr)
            continue

        for candidate in candidates:
            resolved = candidate.resolve()

            if resolved not in seen:
                seen.add(resolved)
                result.append(candidate)

    return sorted(result)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Insert a blank line before braced if/else statements, "
            "after their opening brace, and before their closing brace."
        )
    )
    parser.add_argument(
        "paths",
        nargs="+",
        help="C/H files or directories",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="report files that would change without writing them",
    )
    parser.add_argument(
        "--backup",
        action="store_true",
        help="create an adjacent .bak file before modifying a source file",
    )
    args = parser.parse_args()

    files = iter_source_files(args.paths)

    if not files:
        print("No C or header files found.", file=sys.stderr)
        return 2

    changed = 0

    for path in files:
        try:
            original = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            print(f"error: {path}: {error}", file=sys.stderr)
            continue

        formatted = format_text(original)

        if formatted == original:
            continue

        changed += 1
        print(path)

        if args.check:
            continue

        if args.backup:
            backup = path.with_name(path.name + ".bak")
            backup.write_text(original, encoding="utf-8")

        path.write_text(formatted, encoding="utf-8")

    if args.check and changed:
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
