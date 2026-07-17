Small local LLMs can't hold a big codebase in context. So don't make them try.

That's the idea behind **CppCoder**, a C++23 toolkit I've been building for asking questions about large repos using small, fully local models (via Ollama — no cloud, no API keys, no data leaving your machine).

Instead of stuffing a repo into one context window, it breaks a question into a queue of small research tasks:

→ a **worker** investigates one area at a time, bounded to a fixed token budget
→ a **judge** reviews the finding and prunes anything off-topic before it re-enters the queue
→ surviving directions get requeued, areas are never revisited twice
→ once something useful is found, the accumulated findings get synthesized into one answer

It's the same pattern good human reviewers use on an unfamiliar codebase: look somewhere bounded, decide if it mattered, follow the thread or drop it, repeat.

I just added a second mode: a plain local chat UI (`--serve`) on top of the same Ollama instance, with a model switcher and a small persisted-memory system that lets the assistant remember facts across conversations and model swaps.

108 tests, all offline/pure — the network-facing logic is tested through pure functions so the suite doesn't need a running model.

Still early, still rough in places, but the worker/judge/task-queue loop already holds up on real questions against real repos.

Code's on GitHub: [link]

#cpp #llm #localfirst #ollama #opensource
