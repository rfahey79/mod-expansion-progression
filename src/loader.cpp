// SPDX-License-Identifier: GPL-2.0-or-later
void AddProgressionScripts();
void AddProgressionClampScripts();

void Addmod_progressionScripts()
{
    AddProgressionScripts();
    AddProgressionClampScripts();
}

// Also support cloning the GitHub repository without renaming its directory.
void Addmod_expansion_progressionScripts()
{
    AddProgressionScripts();
    AddProgressionClampScripts();
}
