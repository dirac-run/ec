# Measured model results

These are local research measurements of trained checkpoints, not untouched
upstream Hugging Face models. ALFA-updated has 300 public tasks; errors remain
in that denominator. The internal total covers six panels: commands 244,
quoting 256, exact operands 512, time 84, transfers 160 and English wording 64.
Related templates are correlated, and the panels were consulted during
development. They are not 1320 independent unseen requests.

The publication choices are **0.6B A1 (Q4/Q8)** and **1.5B A3 (Q4)**.
The other rows below preserve research comparisons; they are not additional
checkpoints in the initial model release.

Public downloads: [0.6B GGUFs](https://huggingface.co/dirac-run/ec-0.6b-gguf),
[1.5B GGUF](https://huggingface.co/dirac-run/ec-1.5b-gguf), trainable
[0.6B](https://huggingface.co/dirac-run/ec-0.6b) and
[1.5B](https://huggingface.co/dirac-run/ec-1.5b), and the
[401,975-pair dataset](https://huggingface.co/datasets/dirac-run/ec-training-data).

| Model/checkpoint | Quantization | ALFA-updated /300 | Internal /1320 |
|---|---|---:|---:|
| 0.6B A1 | Q4_K_M | 165 | 911 |
| 0.6B A1 | Q8_0 | 174 | 922 |
| 0.6B A3 | Q4_K_M | 162 | Unmeasured |
| 0.6B A3 | Q8_0 | 173 | 936 |
| 1.5B parent, earlier run | Q4_K_M | 210 | 1127, older prompt |
| 1.5B parent, latest matched run | Q4_K_M | 208 | 1115 |
| 1.5B supported repair A3 | Q4_K_M | 212 | 1119 |
| 1.5B find specialization | Q4_K_M | 217 | 1056 |

A1 and A3 identify different incremental checkpoints; Q4 and Q8 of the same
checkpoint share trained weights before quantization. The 1.5B parent is the
earlier trained step-14833 state. The repair A3 continues it for 200 updates;
the find specialization adds another 200 updates to A3.

The current system prompt is in `config/system-prompt.txt`. It is 177 bytes,
with SHA256 `3a9028d5aebb73c3ed7363e63eb3e751689218775ea5572ab0dc6807e522238b`.
Evaluations used greedy generation, no grammar, a 256-token limit and the
native CPU decoder, with Qwen3 thinking disabled. The historical parent
1127 internal result used a different 615-byte prompt and must not be presented
as a matched current-prompt result. The repeated 208 and earlier 210 ALFA
measurements are reported separately rather than selecting the higher run.

Both 0.6B Q8 runs had two benchmark errors; the Q4 rows in the table had
one. A live reference timestamp caused one unchanged-answer grade flip
in an A3 BF16/Q8 comparison; this is not evidence that quantization improved
the underlying command.

The newly rounded merged BF16 HF exports passed loading/generation and tensor
checks. Their original FP32 adapters passed gradient checks without an optimizer
update. These exports have no fresh full-suite BF16 measurements; they do not
inherit the GGUF scores or historical GPU scores.

## Complete comparison with whatisit

| Deployed configuration | ALFA-updated /300 | Commands /244 | Quoting /256 | Operands /512 | Time /84 | Transfer /160 | English /64 | Internal /1320 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| EC 1.5B A3 Q4_K_M | 212 | 232 | 237 | 417 | 80 | 89 | 64 | 1119 |
| EC 0.6B A1 Q8_0 | 174 | 219 | 217 | 272 | 73 | 77 | 64 | 922 |
| EC 0.6B A1 Q4_K_M | 165 | 217 | 202 | 284 | 72 | 72 | 64 | 911 |
| [whatisit / nl2sh-1.5b Q4_K_M](https://huggingface.co/ThorOdinson246/nl2sh-1.5b-Q4_K_M) | 191 | 80 | 0 | 0 | 2 | 0 | 0 | 82 |

All ALFA rows use [ALFA-updated](https://github.com/dirac-run/ALFA-updated),
grading-definition SHA256
`6f50e4fea37c125f73f93e5f785e95e91fda448b4bf347a9a2986db4d83b48dc`.
The whatisit ALFA run has five errors; its internal run has one time-observer
error, retained in the full denominator. Its current internal result grades all
six current panels: 1,160 exactly matching saved request/output pairs and 160
new transfer outputs, all scored with the frozen fixture policies.

This compares deployed profiles, not identical inference configurations.
EC uses the 177-byte JSON system prompt with a 256-token limit. Whatisit uses
its native plaintext prompt, 64-token limit, repetition penalty 1.08 and
newline/end/fence stops. Its commands are admitted without EC JSON requirements
and remain unchanged. Thirty answers hit its output limit; the other failures
include incorrect quoting, altered or omitted operands, wrong flags and
incomplete operations.

The internal panels deliberately stress exact literal arguments and modifiers.
Quoting and operands alone account for 768/1320 rows. EC development repeatedly
consulted these correlated panels. The total is evidence on those contracts,
not a general shell-competence percentage or an independent comparison of
training quality. The [project post](../PROJECT_POST_DRAFT.md) separately lists
author-reported original-ALFA references; do not merge the two scoring protocols.

## What worked, and what did not

- Simpler prompts and command-only behavior helped some checkpoints. Output
  parsing and correct chat-template boundaries mattered substantially.
- Corrected argument, scope and find examples helped, but narrow improvements
  could damage unrelated commands. The find specialist's higher ALFA score
  came with weaker internal retention, so it was not selected as the default.
- Lower-rate short continuations with replay were useful experiments. They
  did not establish a universal recipe or reach the 230/300 target.
- The deployed precision matters: BF16, Q4 and Q8 could rank checkpoints
  differently. Report each precision rather than implying all quants inherit
  the full-fidelity result.
- A full plain-command continuation scored 212 BF16 / 210 Q4 on ALFA, versus
  its matched JSON parent's 207 / 208. Its internal results were 1023 / 1009,
  below the parent's 1055 / 1115. Removing JSON alone did not establish a broad
  improvement; that experiment also changed training exposure and history.

These scores used the documented ALFA-updated variant and must not be compared
directly with original ALFA's published scores as if the protocols were identical.
CLI validation is an additional product behavior; benchmark measurements evaluate
saved model commands in the benchmark environment. Different CPU builds or
inference implementations can change model outputs and require re-evaluation.
