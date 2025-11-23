#!/usr/bin/env python3
"""
concat_cpp_c_h.py

Recursively concatenate all C/C++ source and header files under a directory
into a single text file.

By default:
- Searches the directory where this script lives (recursively).
- Writes output file all_cpp_c_h_code.txt in the same directory as the script.

Usage:
    python concat_cpp_c_h.py

Optional:
    python concat_cpp_c_h.py -r PATH/TO/ROOT -o custom_name.txt
"""

import argparse
import os
from pathlib import Path
from typing import List, Set

INCLUDE_EXTS: Set[str] = {
    ".c", ".cc", ".cxx", ".cpp",
    ".h", ".hh", ".hxx", ".hpp",
}

DEFAULT_EXCLUDE_DIRS = {
    ".git",
    ".hg",
    ".svn",
    "build",
    "dist",
    "__pycache__",
    ".idea",
    ".vscode",
    "cmake-build-debug",
    "cmake-build-release",
}


def find_source_files(root: Path, exclude_dirs: Set[str]) -> List[Path]:
    result: List[Path] = []

    for dirpath, dirnames, filenames in os.walk(root):
        dirpath_path = Path(dirpath)

        # prevent walking into excluded dirs
        dirnames[:] = [d for d in dirnames if d not in exclude_dirs]

        dirnames.sort()
        filenames_sorted = sorted(filenames)

        for filename in filenames_sorted:
            path = dirpath_path / filename
            if path.suffix.lower() in INCLUDE_EXTS:
                result.append(path)

    return result


def main() -> None:
    # Directory where the script file lives
    script_dir = Path(__file__).resolve().parent

    parser = argparse.ArgumentParser(
        description="Concatenate all C/C++ sources and headers into one text file."
    )
    parser.add_argument(
        "-r", "--root",
        type=str,
        default=str(script_dir),
        help="Root directory to search (default: directory containing this script).",
    )
    parser.add_argument(
        "-o", "--output",
        type=str,
        default="all_cpp_c_h_code.txt",
        help="Output text file name (saved next to this script).",
    )
    parser.add_argument(
        "--include-hidden",
        action="store_true",
        help="Include hidden directories (starting with a dot).",
    )

    args = parser.parse_args()

    root = Path(args.root).resolve()
    # Always save output next to the script
    output_path = script_dir / args.output

    exclude_dirs = set(DEFAULT_EXCLUDE_DIRS)

    if not args.include_hidden:
        # Skip directories whose names start with '.' under root
        for d in root.iterdir():
            if d.is_dir() and d.name.startswith("."):
                exclude_dirs.add(d.name)

    files = find_source_files(root, exclude_dirs)

    # Do not accidentally include the output file itself
    files = [f for f in files if f.resolve() != output_path.resolve()]

    files.sort(key=lambda p: str(p.resolve()))

    print(f"Script directory: {script_dir}")
    print(f"Root search directory: {root}")
    print(f"Found {len(files)} source/header files")
    print(f"Writing concatenated output to: {output_path}")

    with output_path.open("w", encoding="utf-8", errors="replace") as out_f:
        for idx, file_path in enumerate(files, start=1):
            full_path = str(file_path.resolve())

            out_f.write("=" * 80 + "\n")
            out_f.write(f">>> BEGIN FILE {idx}/{len(files)}: {full_path}\n")
            out_f.write("=" * 80 + "\n\n")

            try:
                with file_path.open("r", encoding="utf-8", errors="replace") as in_f:
                    out_f.write(in_f.read())
            except OSError as e:
                out_f.write(f"\n[ERROR READING FILE: {full_path}]\n{e}\n")

            out_f.write("\n\n" + "=" * 80 + "\n")
            out_f.write(f">>> END FILE {idx}/{len(files)}: {full_path}\n")
            out_f.write("=" * 80 + "\n\n")

    print("Done.")


if __name__ == "__main__":
    main()
