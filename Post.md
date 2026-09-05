CppLocalLlmCodeAssist



CppLocalLlmCodeAssist (cppcoder) is a C++23 orchestration framework for operating small, local LLMs (via Ollama) against large codebases. To overcome the usable context-window constraints of small models, the system replaces single-shot context dumps with bounded, multi-pass architectures, structured task queues, and deterministic retrieval pipelines.



Operating Modes & System Architecture

1. Research Mode (Bounded Task Queue Engine)

Implements a worker/judge architecture designed for sustained codebase exploration:



Keyword-Seeded Discovery: Extracts identifier terms from user queries to seed initial investigation targets via file content grepping.



Bounded Context Workers: Constrains worker evaluation passes to a strict token budget (~120K tokens) to maintain model reasoning fidelity.



Relevance Judge: Executes a secondary pass on worker output to filter hallucinated or off-topic investigation paths before re-enqueuing surviving tasks.



Deterministic Synthesis: Continuously drains the queue, tracking visited target areas to prevent cyclic evaluations until findings are synthesized into a final answer.



2. Edit Mode (Full-Content Substitution)

Drives file modifications using the same task-queue loop as research mode:

Zero-Diff Integrity: Generates complete replacement file contents rather than diffs, avoiding patch-parsing failures common in small local models.



Dry-Run Default: Outputs proposed changes to stdout for developer review unless explicitly instructed to write to disk (--apply).



Traversals & Confinement: Uses a path-applier subsystem that strictly enforces codebase boundary rules to reject directory traversal attempts.



3. Repository-Aware Chat Server & CLI

Provides streaming web (web/chat.html) and terminal REPL interface modes:



Content-Grep Retrieval Pre-Pass: Identifies relevant context using content-based keyword matching (filtering conversational noise and saturated terms) to inject targeted source files prior to message forwarding.



Context Intercepts: Handles local slash commands (/pwd, /ls, /read, /write) inside the hosting process without making LLM round trips, strictly confined to the enclosing Git repository root.



Fact Memory Persistence: Automatically extracts and maintains key contextual facts across model switches, persisting data locally to memory.json.



Technology Stack & Build Pipeline



Language Standard: C++23 (Modules, Concepts, Coroutine-ready infrastructure).



Core Dependencies: spdlog (structured logging), nlohmann_json (de/serialization), cpp-httplib (HTTP/REST transport), and googletest (unit testing suite).



Build System: Cross-platform CMake (>= 3.24) with Ninja driver support (t.ps1 automation suite).



Direct Links



Repository: github.com/cschladetsch/CppLocalLlmCodeAssist

README Documentation: CppLocalLlmCodeAssist/README.md


