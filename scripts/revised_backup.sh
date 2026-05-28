#!/bin/bash

set -euo pipefail

# Resolve base directory (parent of this script's directory)
base_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
backup_dir="$base_dir/backup"

echo "Base directory: $base_dir"

# Ensure the backup destination exists before writing anything
mkdir -p "$backup_dir"

# Step 1: Clean source directory
cd "$base_dir/src"
make wipe

# Step 2: Clean area directory
rm -f "$base_dir/area/core"

# Step 3: Create backup timestamp
backup_timestamp=$(date "+%d_%b_%Y_%H_%M")

# Step 4: Create per-directory backups directly into backup/
# Archives go to backup/ from the start — no intermediate mv needed,
# and no risk of a directory's own archive being included in itself.
backup_directories=("player" "area" "src")
for dir in "${backup_directories[@]}"; do
    archive="$backup_dir/Project-Twilight-2-$dir-$backup_timestamp.tgz"
    tar -czf "$archive" -C "$base_dir" "$dir/"
    chmod og-rwx "$archive"
done

# Step 5: Create full MUD backup
# -C archives relative to base_dir so --exclude paths match what tar records.
# Exclude log/ (runtime noise) and backup/ (old archives would inflate every run).
mud_archive="$backup_dir/Project-Twilight-2-MUD-$backup_timestamp.tgz"
tar -czf "$mud_archive" \
    -C "$base_dir" \
    --exclude='./log' \
    --exclude='./backup' \
    .
chmod og-rwx "$mud_archive"

# Step 6: Rebuild source
cd "$base_dir/src"
make

# Step 7: Show results
echo "Backup files created:"
ls -lh "$backup_dir/"*"$backup_timestamp"*

# Step 8: Remove backup-in-progress sentinel only on clean completion.
# With set -e, a failure above aborts before reaching this line.
rm -f "$base_dir/src/completedbackup"

echo "Backup completed at $backup_timestamp."
