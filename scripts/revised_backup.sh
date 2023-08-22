#!/bin/bash

# Set the base directory to the parent of the script's directory
base_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Base directory: $base_dir"

# Step 1: Clean source directory
cd "$base_dir/src" || exit
make wipe

# Step 2: Clean area directory
cd "$base_dir/area" || exit
rm -f core

# Step 3: Create backup timestamp
backup_timestamp=$(date "+%d_%b_%Y_%H_%M")

# Step 4: Create backups
backup_directories=("player" "area" "src")
for dir in "${backup_directories[@]}"; do
    tar -cvzf "Project-Twilight-2-$dir-$backup_timestamp.tgz" "$base_dir/$dir/"
    chmod og-rwx "Project-Twilight-2-$dir-$backup_timestamp.tgz"
done

# Add a sleep command to introduce a delay
sleep 5


# Step 5: Create MUD backup
tar -cvzf "Project-Twilight-2-MUD-$backup_timestamp.tgz" --exclude="$base_dir/log" "$base_dir/"
chmod og-rwx "Project-Twilight-2-MUD-$backup_timestamp.tgz"

# Step 6: Rebuild source
cd "$base_dir/src/" || exit
make

# Step 7: Move backups
mv "$base_dir"/area/*.tgz "$base_dir/backup/" 2>/dev/null

# Add a sleep command to introduce a delay
sleep 2

echo "Backup files after moving:"
ls -l "$base_dir/backup/"

# Step 8: Clean up
rm -f "$base_dir/src/completedbackup"

# Done
echo "Backup completed at $backup_timestamp."
