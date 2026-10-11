# AGENTS.md — Coding Agent Guidelines for Whisper64-Extended

**Project:**  — A feature-rich text editor for the Commodore 64 with extended BASIC functions. Forked from https://github.com/sandlbn/whisper64. Targeted to run on real Commodore 64 hardware (primary
target: C64 Ultimate FPGA)
**Primary Framework:** 
**Status:** 
**Last Updated:** October 11, 2026

This file defines how coding agents (Grok Build, Cursor, Claude, human contributors, etc.) should work on the project. It exists to keep changes consistent, high-quality.

**IMPORTANT:**

- Always check available skills and invoke them when relevant to the user's request.
- If adding a new crate explain to the user why it is needed or should be used compared to what it will take to do the work without it and give them the option to say yes or no to its inclusion.
- Delete dead or obsolete code, unreachable code, or functions that are never called.
- Code must look hand-written for this repository. Changes that read like generic AI-generated code will be rejected automatically: unnecessary helper layers, vague names, boilerplate comments, defensive branches without a real failure mode, broad rewrites, or code that ignores the local style.
- For small one file changes, they can be preformed on the main git branch.
- For medium to large multi file changes and refactors use writing-plans, executing-plans and subagent-driven-development skills.
- Always use a Code reviewer subagent to review the work afterward. Create a commit once work has been validated.
- Verify that any changes do not break existing functionality.
- Keep this AGENTS.md file updated with information about the project that other AI agents would need to know to keep the project consistent from session to session. (Project layout/structure, technology stack, tool chain, coding standards & style, testing, pitfalls and conflicts to avoid, features, etc.)
- When a bug is fixed or feature is implemented check it off the users `docs/Todo-List.md` and include it in the commit for for the final work.

---

## Project Structure
