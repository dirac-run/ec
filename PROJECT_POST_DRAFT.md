# Training a small Bash assistant: what we learned from 400,000 examples

*Post draft covering completed research from 19 September–5 October 2026. Results are locally measured unless explicitly described otherwise. Source repositories: [EasyCommand (`ec`)](https://github.com/dirac-run/ec) and [ALFA-updated](https://github.com/dirac-run/ALFA-updated). Model weights and teaching data are published separately on Hugging Face. The [results guide](https://github.com/dirac-run/ec/blob/main/docs/RESULTS.md) records checkpoint and precision comparisons.*

The project started with an everyday annoyance: looking up a Bash command for something we knew the shell could do, but could not remember how to express. We wanted a small model that could live on the machine, run on the CPU, turn an English request into a command, and let the user inspect it before execution.

We started with a 135-million-parameter model and fewer than 10,000 training examples. Over the following weeks, we expanded the data, tested several architectures and training methods, built executable benchmarks, and investigated failures down to individual command arguments. The [published dataset](https://huggingface.co/datasets/dirac-run/ec-training-data) contains **401,975 distinct request/answer pairs**.

We released a 1.5B four-bit model scoring **212/300 on ALFA-updated** and **1119/1320 on our internal suite**. ALFA-updated is a documented repair of the original benchmark. A research checkpoint reached **217/300**, but lost 63 internal passes relative to the release choice. Our released 0.6B model scored **165/300** at four bits and **174/300** at eight bits. The highest score on one benchmark did not consistently identify the model we preferred for everyday use.

The recurring challenge was learning new distinctions while retaining existing skills and carrying them over to unfamiliar requests. A model could learn nearly every targeted training example, improve a development score, and still lose useful behavior elsewhere.

## Starting small

The first model was SmolLM2-135M-Instruct. We trained it locally on a Ryzen 7 3800X, using full-parameter fine-tuning. Eight training threads were faster than sixteen on that machine, an early reminder to measure the actual implementation rather than assume more parallelism helps.

The first substantial run used 9,809 examples and processed roughly 290 tokens per second. At an early checkpoint, the model passed 9 of 17 previously seen command requests and none of 17 held-out requests. The pipeline worked, but generalization was weak.

More balanced data eventually improved a separate diagnostic from 35.3% to 59.2%. More epochs did not keep improving it: a later 264-case benchmark fell from 152 passes to 149 and then 138 across three epochs. That pattern returned throughout the project.

We moved to SmolLM2-360M, then Qwen models at 0.6B, 0.8B, 1.5B, and 2B. We also trained a MiniCPM 1B comparator. The early stages used full-parameter training; later campaigns made extensive use of LoRA, which trains added adapter parameters while retaining the original model weights.

The eventual four-model campaign started each model from its own pinned Hugging Face download. These were different model families and training histories, not a controlled test of parameter count alone. The 1.5B initializer was specifically **Qwen2.5-Coder-1.5B-Instruct**.

## Expanding the training data without confusing quantity with coverage

We grew the teaching data in stages. Larger models generated requests and answers; separate review and admission steps determined what entered training. We used execution checks where available, and later also admitted independently reviewed examples using semantic review and retained evidence. We did not execute every row individually.

The coverage expanded from text processing and basic file operations into discovery, archives, Git, processes, networking, remote operations, quoting, temporal predicates, and multi-step workflows. Within a task family, we varied wording, operands, patterns, and constraints.

| Milestone | Count | What the count represents |
|---|---:|---|
| First substantial CPU run | 9,809 | Training examples |
| Preserved seed corpus | 29,424 | Accepted rows |
| First broad coverage expansion | 131,571 | Seed plus 102,147 newly reviewed rows |
| Later shared teaching corpus | 147,748 | A subsequent expanded training revision |
| 0.8B direct-training corpus | 221,525 | Rows in that model's later recipe |
| Revised four-model training epoch | 474,635 | Weighted presentations, including exact repeats |
| Distinct pairs in that epoch | 400,185 | Exact request/answer pairs after deduplication |
| Published dataset with incremental additions | 401,975 | Deduplicated union, including data from other repair experiments |

These counts come from different revisions and model branches. They are not one uninterrupted, uniformly sampled training sequence. A different description of the same command remains a distinct pair; repeating both the identical description and answer adds exposure, not new content.

The published union adds 1,790 new repair pairs to the 400,185 distinct full-run pairs. It contains **154,807 distinct command strings** and occupies **160.2 MiB** as JSONL. We removed exact pair duplicates while retaining different descriptions of the same command. The export contains only request and COMMAND response, with one training split; it does not reproduce historical weighting or any selected checkpoint's complete sampling history.

Before publication, we screened the data for personal information and credentials and replaced the project's personal home prefix with `~/`. Screening found no confirmed live credentials or genuine personal records; this is not proof that every possible identifier is absent. There were zero exact or case/whitespace-normalized matches to the 300 canonical ALFA request strings. However, ALFA failures informed repairs and selection, so that narrow overlap check does not make ALFA an independent final test.

An early plan called for about 330,000 rows. We stopped that expansion at 131,571 after changing the completion criterion to useful coverage rather than a fixed count. The directory retained its old “330k” name, which later caused understandable confusion.

We also rewrote long training requests into more concise English while preserving most command targets. The older descriptions were retained. Mixing original and concise descriptions seemed sensible: it supplied different ways to ask for the same behavior without discarding the earlier data.

However, the 2B results were modest. Under the original ALFA protocol, the trained baseline scored 153/300, a concise-description continuation scored 149, and a combined original-plus-concise continuation scored 156. Quoting improved substantially, while some other skills regressed. Both continuations began from the same earlier trained checkpoint; the combined run also supplied more exposure, so it did not isolate the effect of wording diversity.

The lesson was that an appealing data intervention still needed measurement. Two descriptions per command could help, but they did not automatically preserve every behavior the model already had.

## Expanding the tests as well as the teaching data

Training examples show the model what to produce. Test examples tell us whether it learned the intended behavior. We expanded both, keeping their roles explicit.

Our tests increasingly used isolated filesystem and process fixtures. A command could be checked for its output and side effects: which files changed, whether contents and timestamps survived, whether a directory was omitted, or whether an unrequested deletion occurred. Where available, multiple layouts and adverse examples helped distinguish a correct command from one that happened to work on a convenient fixture.

The later internal suite had six panels:

| Panel | Cases | Main purpose |
|---|---:|---|
| Main command benchmark | 244 | Ordinary shell behavior under its existing admission and fixture rules |
| Quoted literals | 256 | Quoting and special-character handling |
| Exact operands | 512 | Preserve filenames, paths, and search text |
| Time operations | 84 | Temporal predicates and units |
| Transfer | 160 | Related generalization probes |
| English wording | 64 | Two wording frames across 32 operand contracts |
| **Total** | **1,320** | Development and retention evidence |

This total is a count of benchmark rows, not 1,320 independent unseen tasks. Many examples share templates, and we consulted these panels repeatedly. The exact-operand panel contributes 38.8% of the pooled score, so we also examined individual panels and, later, an equal-weight panel average.

We added fresh confirmation panels for selected candidates. One late find-focused comparison used 120 requests spanning 20 families and three layouts, opened only after the winner was frozen. Its result was sobering: the benchmark winner scored 28/120 versus its parent's 27, with 18 gains and 17 losses. There was little evidence of a broad breakthrough.

## The clearest early finding: fitting examples is easier than transferring them

One 0.6B targeted practice experiment made the distinction unusually clear:

| Diagnostic | Before | After |
|---|---:|---:|
| Time-related training requests | 8/72 | 64/72 |
| Literal-text training requests | 27/48 | 48/48 |
| Separate time-transfer requests | 1/18 | 1/18 |
| Routine commands | 70/102 | 60/102 |

The model could learn the supplied answers. It did not acquire reliable transfer on the separate time requests, and routine performance declined.

Later matched 0.6B runs fit all 192 new training probes and 132 old probes, while passing only 29/58 or 30/58 transfer requests. Changing loss weighting from answer-token averages to equal example weighting did not establish an overall improvement.

This changed our questions. “Is the loss decreasing?” remained useful, but we also needed to ask whether the model chose the correct operation, retained literal arguments, respected every modifier, and combined familiar operations under new wording.

It also argued for reporting gains and losses separately. A net improvement of a few cases can hide dozens of changed successes and failures.

## Why the find commands became an important investigation

During informal checks and ALFA failure review, many generated `find` commands looked elaborate. We initially suspected either bad labels or an overcomplicated training distribution.

The distribution audit found 75,759 find-related exposures in a 456,395-presentation epoch. Among those, 76.7% used pipelines and 31.7% used loops. Three generator families accounted for 31.4% of all find exposures.

Length alone was not the explanation: median find command length was 117 characters, versus 122 for non-find commands. The stronger concern was the repeated structure—sorting, aggregation, NUL-delimited loops, and detailed reporting—even around fairly ordinary requests.

For example, a generated regular-file count included:

```bash
find . -mindepth 1 -type f -printf x | wc -c | awk '{print $1}'
```

When `.` is the directory being searched, `-mindepth 1` does not change the regular-file selection, and the final `awk` merely projects an already scalar count. The `-printf x` technique, however, serves a purpose: filenames containing newlines do not inflate the count. “Simpler” needs to preserve the request's behavior, not merely remove every unusual construct.

The audit also found genuine errors: counting files instead of matching lines, mixing producer and consumer delimiters, choosing the wrong scope, dropping constraints, or performing an incorrect mutation. In a static review of 118 find outputs, 56 were erroneous and 29 ambiguous; the remaining 33 were correct, sometimes with justified complexity and sometimes with redundancy.

We organized seven parallel data workstreams around find simplification, operation/measurement units, operand roles, composition, CLI/platform behavior, exact bytes, and conditional execution.

Their first package contained only 102 new wordings for 51 contracts. That sounded small next to a 224k-scale corpus, and it was: the initial package did not by itself reshape the corpus. We then integrated and expanded it.

The completed intervention added 1,140 wordings, supplied 18,240 weighted repair presentations, and simplified 1,662 historical source presentations using narrow, qualified rules. It reduced the three concentrated find families while preserving total legacy find exposure. The new repairs comprised 3.84% of examples and 2.45% of supervised answer tokens.

This avoided regenerating everything. It also gave us a precise description of the change, rather than merely saying we had “better data.” The full-run benefit of every component was not separately established.

## What helped, and what did not reliably help

The project included many small experiments, not just successive full runs. An initial A40 suite contained 14 training experiments; a later qualification suite added 18 arms across three A40s. We then completed six 1.5B and three 0.6B incremental attempts, followed by 15 additional 1.5B find-focused attempts.

| Intervention | What we observed | What we can conclude |
|---|---|---|
| Broader teaching coverage | Substantial improvement over the earliest models | Useful in the evolving project; model size, data, and training history also changed |
| Verified, targeted repairs | The 1.5B supported A3 improved BF16 ALFA 207→217 and internal 1052→1073 | A positive result for that recipe; Q4 ALFA improved only 210→212 |
| Lower learning rate | Matched find arms retained 1062, 1029, and 1005 internal passes at LR 5e-6, 2e-5, and 5e-5 | Larger updates increased retention cost in these short trials |
| More replay / different repair fractions | Some skills survived better; 12.5% repair was more consistent than 25% in a two-seed short pilot | Mixture matters; no universal best ratio was established |
| Longer training | Several later checkpoints lost to earlier ones; 600-update repairs lost to 200-update counterparts | More exposure is not a guaranteed improvement |
| LoRA | Practical full-corpus training and fast continuations | A useful method, without proof it always beats full fine-tuning on matched data |
| Adaptive learning rate | One short arm did not outperform the selected cosine recipe | No demonstrated advantage here; too little evidence for a general rejection |
| Reasoning supervision | The matched 0.8B reasoning-trained model scored 149 original ALFA versus control 142 with direct answers | Training-time reasoning helped in this experiment |
| Reasoning at inference | The same 0.8B model scored 135 when producing bounded reasoning first | Extra reasoning output did not help that protocol |
| Auxiliary objectives, typed plans, preference training | Tried in the 360M phase; related fit improved in places, without a broad replacement | Interesting mechanisms, but no general win in those completed trials |
| Masking repeated JSON wrapper tokens | The initial diagnostic worsened ALFA and quoting | Removing their loss did not establish the proposed bottleneck explanation |
| Plain command output | Small ALFA gains, but weaker internal retention | No demonstrated overall improvement from removing JSON |

A shorter system prompt was another useful but model-dependent change. Under the updated grader, it moved 1.5B Q4 from 203 to 210 and 2B from 181 to 188. It moved 0.6B from 161 to 158 and 0.8B from 159 to 154. A better prompt for one model was not automatically better for another.

The released models use this exact serving prompt:

```text
You are a GNU/Linux shell command generator. Produce the simplest Bash command that fulfills the entire request. Return only valid JSON: {"kind":"COMMAND","value":"<command>"}.
```

The release measurements use greedy generation, a 256-token output limit, and no JSON grammar. Qwen3 thinking is disabled for 0.6B; 1.5B uses its ordinary Qwen2.5 ChatML template. These details are saved with the models because a prompt or decoder change can alter a score without changing the weights. The parent full-corpus training used an older, longer prompt; the incremental release training used this shorter one.

## The benchmark needed investigation too

We used ALFA, a 300-task shell benchmark, as an external reference. Its original grading combined command execution, state checks, and semantic output comparison.

When manual command review disagreed with ALFA, we investigated rather than treating manual review as definitive. We found real model errors that reviewers had missed, along with tool gaps, command-transport defects, fixture-reset problems, reference errors, and output checks that could accept wrong answers or reject correct alternatives.

An independently written gold record initially scored 232/300 while the original reference commands scored 296. That disparity became an evaluator investigation, not an invitation to train the model on those 300 answers.

We created **ALFA-updated**, keeping the request set and submitted model outputs unchanged. Its documented changes included faithful Bash argument transport, verified state restoration, uniform standard-tool installation, explicit reference errata, complete filesystem checks, and correctness checks for dynamic or equivalent outputs. Changes were made between runs, never by patching an active run or editing predictions.

The updated grader uses documented correctness checks for specified cases and retains the original embedding comparator elsewhere. We did not add an LLM judge. It is a different, disclosed benchmark definition; its scores must be reported separately from upstream ALFA.

| Same saved model outputs | Original ALFA /300 | ALFA-updated /300 |
|---|---:|---:|
| Our 0.6B Q4, older prompt | 133 | 161 |
| Our 1.5B Q4, older prompt | 172 | 203 |
| External whatisit 1.5B, its native output protocol | 179 | 191 |

The external model received the same updated grader. All denominators remained 300, including infrastructure errors: one for each of those two EasyCommand runs and five for the external run. Some repairs removed false passes, so the changes did not only increase scores.

The final reference self-control reached 299/300, with an unavailable external HTTP resource remaining. The [public change log](https://github.com/dirac-run/ALFA-updated/blob/main/CHANGELOG.md), [correctness reasoning](https://github.com/dirac-run/ALFA-updated/blob/main/REASONING.md), and [known limitations](https://github.com/dirac-run/ALFA-updated/blob/main/docs/KNOWN_LIMITATIONS.md) explain the repaired definition. The repository's release includes the pinned Docker and embedding assets needed for setup. Independent replication remains outstanding; public source availability alone does not establish it.

One other comparison proved misleading: our trained 1.5B looked barely above a published untuned score. But our own stock-model evaluation had rejected all 300 Markdown-fenced JSON answers before command grading. The published and local interfaces differed. We still lack a clean, matched stock 1.5B baseline; subtracting the published number from ours does not establish the effect of training.

We also found an unchanged 0.6B command whose grade flipped when timestamps in the reference output moved its embedding similarity across the threshold. The raw score stayed recorded, but that case was not credited as a learning or quantization gain. Per-case evidence mattered even after the evaluator repairs.

## Three surprises changed our decisions

### The replay data did not replay find

In the fifteen-attempt find repair campaign, the 1,900-row retention mixture contained **zero find commands**. Its inherited selector explicitly excluded every command containing `find `, including short accepted examples.

That was a concrete omission in an experiment intended to improve find while preserving existing skills. We found 320 receipt-backed old find examples spanning 64 families that could support a reviewed replacement mixture. We had not yet tested adding them at this snapshot, so their expected benefit remains a hypothesis.

### Quantization could change the winning checkpoint

The 1.5B BF16 leaders reached 219 ALFA, but their Q4 versions scored 210 and 215. Two other arms tied at 217 Q4 despite scoring 211 and 214 in BF16. Selecting by the full-fidelity score alone would have selected a different deployment artifact.

For 0.6B, A3 led BF16 ALFA at 177, while A1 led Q4 at 165. A1 Q8 recovered 174, and A3 Q8 scored 173 with better internal retention.

These comparisons bundle precision, adapter merging/casting, backend, and sometimes batching. We cannot attribute every change solely to four-bit arithmetic. They do establish why the actual exported model needs its own evaluation.

### Removing JSON did not solve the problem we expected

Initially JSON allowed three response categories: command, clarification, and inability. Later we changed the policy to always propose a best-effort command, while retaining the constant JSON envelope. That made plain-text output an obvious hypothesis to test.

We ran a matched 400-update pair from the trained 1.5B parent: identical 12,800 request/command pairs and training recipe, with JSON versus plain targets and the appropriate system prompt. Then we continued the plain pilot for one pass over all 401,975 unique pairs.

| 1.5B format experiment | ALFA BF16 /300 | ALFA Q4 /300 | Internal BF16 /1320 | Internal Q4 /1320 |
|---|---:|---:|---:|---:|
| JSON parent | 207 | 208 | 1055 | 1115 |
| JSON 400-update control | 207 | 205 | 1037 | 1057 |
| Plain 400-update treatment | 209 | 210 | 1016 | 1014 |
| Full plain continuation | 212 | 210 | 1023 | 1009 |

The full continuation improved some ordinary-command and quoting cases, but exact-argument performance stayed below the JSON parent. Missing leading hyphens and shortened filename segments remained command-content errors.

We checked whether the internal suite directly favored JSON. A native plain-command adapter replay covering 768 quote/operand cases produced zero score differences. An equal-weight panel average also retained the plain Q4 regression, so it was not explained solely by the large operand panel.

The full continuation began from the plain pilot, not stock HF weights. It cannot isolate format from additional exposure and accumulated history. Still, neither the matched pilot nor the full continuation supported automatically promoting the plain model as an overall improvement.

## Iteration speed became part of the research method

Full training runs were too slow to use for every hypothesis. A cheap A40 could complete an 80-update pilot in about nine minutes. We used those runs to screen learning rates, sampling, objectives, replay, and runtime changes before committing to full-corpus work.

For the four-model campaign, H100 trained continuously while separate A40s handled evaluation. That policy came after an operational mistake: an earlier quarter-checkpoint test held training behind evaluation while the H100 was still rented. We corrected it. Final checkpoints took priority over unfinished intermediate evaluation, and training no longer waited for benchmark grading.

We tuned batches and kernels per model, avoided unnecessary activation checkpointing, and reduced routine disk writes. Full-fidelity weights and evidence were archived locally before deleting machines and attached storage. Local quantized inference and CPU grading finished work that did not require a rented GPU.

The latest full plain 1.5B continuation took **48.3 minutes of H100 training**. Its total estimated GPU rent, including an unsuccessful startup allocation and separate A40 evaluation, was **$3.39**, excluding storage and invoice reconciliation. This is one measured experiment's cost, not the project's total spending.

Cheap, informative iterations mattered more than simply making one full run finish faster. They let us reject plausible explanations and investigate concrete failure mechanisms.

## Where the results ended up

The first table compares the selected four-model checkpoints with the **same older JSON prompt, original ALFA, and historical internal suite**. These are Qwen3-0.6B, Qwen3.5-0.8B, Qwen2.5-Coder-1.5B-Instruct, and Qwen3.5-2B. The 0.8B row uses its retained older reasoning-trained checkpoint; the other rows use the later LoRA campaign. The 2B row is halfway, selected over final because its ALFA result was stronger.

| Model / selected checkpoint | Original ALFA Q4 /300 | Internal Q4 /1320 | ALFA-updated Q4 with later role prompt /300 |
|---|---:|---:|---:|
| 0.6B final14833 | 133 | 931 | 158 |
| 0.8B reasoning7348, direct decode | 145 | 992 | 154 |
| 1.5B final14833 | 172 | 1127 | 210 |
| 2B half7417 | 156 | 962 | 188 |

The internal column used the older prompt. The last column used the later role prompt and updated grader. Reading horizontally is not a training improvement: those model weights did not change. The 1.5B's 1127 must not be advertised as a matched current-prompt internal score.

The later export candidates were:

| Model / checkpoint | Precision | ALFA-updated /300 | Internal /1320 | Measured tradeoff |
|---|---|---:|---:|---|
| 0.6B A1 | Q4 | 165 | 911 | Published; best measured 0.6B Q4 ALFA |
| 0.6B A1 | Q8 | 174 | 922 | Published; best measured 0.6B Q8 ALFA |
| 0.6B A3 | Q4 | 162 | Not measured | Full internal Q4 result missing |
| 0.6B A3 | Q8 | 173 | 936 | Better Q8 internal retention than A1 |
| 1.5B parent14833 | Q4 | 208 | 1115 | Latest matched control; earlier trained checkpoint |
| 1.5B supported A3 | Q4 | 212 | 1119 | Published; selected for balanced results |
| 1.5B find-arm04 | Q4 | **217** | 1056 | Highest Q4 ALFA, weaker broad retention |
| 1.5B full plain continuation | Q4 | 210 | 1009 | Modest ALFA gain versus matched parent, weaker retention |

The parent also has an earlier role-prompt ALFA measurement of 210, shown in the four-model table. The 208 above is the later matched format-experiment control; we preserve both rather than selecting the larger score for every comparison. The rows share the role-prompt style appropriate to their output format, but originate in different experiment runs, and infrastructure effects remain disclosed.

Every ALFA total retains denominator 300. The two 0.6B Q8 runs and full plain Q4 run have two benchmark errors each; the other Q4 rows in this export table have one. The errors are not silently removed to increase pass rates.

The 0.6B A3's best BF16 score was 177. The best 1.5B BF16 score was 219, from different find arms than the Q4 winner. These are historical GPU measurements, not fresh scores for the newly rounded merged BF16 release files. We did not measure 1.5B Q8. No single deployed model reached the 230 target.

The find-arm04 checkpoint illustrates why “best” needs a stated criterion. It added five Q4 ALFA passes over supported A3 but lost 63 net internal passes. Its once-only fresh confirmation improved by just one. We chose supported A3 for publication, with its better balance across panels. A3's small measured gains over the parent do not establish a statistically reliable improvement.

### External comparisons need their protocol attached

Under the same frozen ALFA-updated grader, the external [whatisit / nl2sh-1.5b Q4 model](https://huggingface.co/ThorOdinson246/nl2sh-1.5b-Q4_K_M) scored **191/300**, versus the released A3's **212/300**. Its saved commands were regraded unchanged. The two models use different serving profiles: whatisit has its native plain-command prompt and 64-token limit, while EC uses the JSON prompt and 256-token limit. This compares deployed configurations; it does not isolate training quality under identical inference settings.

We also completed its full current internal suite: **82/1320**, comprising 80 main-command passes and two time-operation passes. It had no passes on quoting, exact operands, transfer, or English wording. We reused 1,160 saved outputs only where both request and case ID matched exactly, generated the missing 160 under its native profile, and regraded all 1,320 with the current fixture policies. One time-observer infrastructure error remains in the denominator.

| Model / quant | ALFA-updated /300 | Main /244 | Quoting /256 | Operands /512 | Time /84 | Transfer /160 | English /64 | Internal /1320 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| EC 1.5B A3 Q4_K_M | 212 | 232 | 237 | 417 | 80 | 89 | 64 | 1119 |
| EC 0.6B A1 Q8_0 | 174 | 219 | 217 | 272 | 73 | 77 | 64 | 922 |
| EC 0.6B A1 Q4_K_M | 165 | 217 | 202 | 284 | 72 | 72 | 64 | 911 |
| whatisit / nl2sh-1.5b Q4_K_M | 191 | 80 | 0 | 0 | 2 | 0 | 0 | 82 |

The large internal gap needs context. These are correlated development panels emphasizing literal operands, quoting, and specific output contracts; we repeatedly used them to guide EC training. They are not a general shell-competence ranking. We checked that native plaintext was admitted without EC's JSON/category requirements: the failures included split space-bearing filenames, missing directories or operands, wrong flags, and incomplete behavior. The native 64-token cap caused additional failures. The complete result is useful evidence about those deployed configurations on these particular contracts, while independent tests and matched inference would support stronger comparisons.

For context, the original whatisit sources and related model cards report the following **original-ALFA** results. These are author-reported measurements using different protocols, not rows that we rescored with ALFA-updated:

| Published model/configuration | Reported download size | Original-ALFA pass rate | Source |
|---|---:|---:|---|
| GPT-4o, cloud API with parsed output | — | 73.0% | [ALFA paper, Table 5](https://aclanthology.org/2025.naacl-long.555.pdf) |
| nl2sh-3b Q4_K_M | 1.9 GB | 65.7% | [whatisit benchmarks](https://github.com/ThorOdinson246/whatisit-nl2sh#benchmarks) |
| nl2sh-qwen25-coder-1.5b, 120k-pool Q4_K_M build | 941 MB | 65.67% | [Model card](https://huggingface.co/barbarabhb/nl2sh-qwen25-coder-1.5b-GGUF) |
| nl2sh-qwen25-coder-1.5b, TPU Q4_K_M build | 0.99 GB | 63.67% | [Model card](https://huggingface.co/barbarabhb/nl2sh-qwen25-coder-1.5b-tpu) |
| whatisit / nl2sh-1.5b Q4_K_M | 941 MB | 62.0% | [whatisit model card](https://huggingface.co/ThorOdinson246/nl2sh-1.5b-Q4_K_M#results) |
| Qwen2.5-Coder-7B-Instruct, untuned | 4.4 GB | 61.3% | [whatisit model card](https://huggingface.co/ThorOdinson246/nl2sh-1.5b-Q4_K_M#results) |
| Qwen2.5-Coder-1.5B-Instruct, untuned | 941 MB | 54.0% | [whatisit model card](https://huggingface.co/ThorOdinson246/nl2sh-1.5b-Q4_K_M#results) |

Download sizes use the authors' units and rounding; the untuned rows' precise quantization is not specified in their table. The GPT-4o row is the paper's parsed-output configuration for `gpt-4o-2024-08-06`; the same table reports 74% for its baseline. These reference numbers do not establish that our model beats GPT-4o or a larger untuned model, or quantify our gain from the stock 1.5B initializer. That requires matched measurements.

### What we published

On 5 October, we published the selected models and dataset on Hugging Face under **Apache-2.0**:

| Released checkpoint | Inference download | Size | ALFA-updated /300 | Internal /1320 |
|---|---|---:|---:|---:|
| 0.6B A1 | [Q4_K_M](https://huggingface.co/dirac-run/ec-0.6b-gguf) | 461.8 MiB | 165 | 911 |
| 0.6B A1 | [Q8_0](https://huggingface.co/dirac-run/ec-0.6b-gguf) | 767.5 MiB | 174 | 922 |
| 1.5B supported A3 | [Q4_K_M](https://huggingface.co/dirac-run/ec-1.5b-gguf) | 940.4 MiB | 212 | 1119 |

Both [0.6B](https://huggingface.co/dirac-run/ec-0.6b) and [1.5B](https://huggingface.co/dirac-run/ec-1.5b) also have complete merged BF16 Transformers checkpoints and their original cumulative FP32 LoRA adapters. The GGUF downloads are standalone merged inference models. The [dataset](https://huggingface.co/datasets/dirac-run/ec-training-data) is in a separate repository, so users can download examples, inference weights, or trainable weights independently.

The model cards include pinned upstream revisions, training lineage, prompts, inference profiles, per-panel scores, licenses, notices, and checksums. We downloaded and rehashed all 65 prepared release files after upload and checked public dataset loading and its row count. The merged BF16 models passed tensor, loading, and generation checks; the original adapters passed finite, nonzero gradient checks without an optimizer update. The newly rounded merged BF16 exports have not received fresh full-suite benchmark runs, so we do not assign them the GGUF scores or historical GPU scores.

All rented GPUs and attached ephemeral storage were deleted after archiving the checkpoints and evidence. The application is published as [ec](https://github.com/dirac-run/ec), with an embedded llama.cpp runtime and a [Linux x86_64 binary release](https://github.com/dirac-run/ec/releases/tag/v0.1.0). It prints the proposed command and asks before executing it; models are downloaded separately. [ALFA-updated](https://github.com/dirac-run/ALFA-updated) has its own [source and runtime-asset release](https://github.com/dirac-run/ALFA-updated/releases/tag/v0.1.0), including the seven checksum-verified archives. Application and benchmark code are MIT licensed; model weights and data are Apache-2.0. Independent benchmark replication remains future work.

## Future directions: using our examples or continuing our models

There are two useful starting points for further work: our teaching dataset and our trained checkpoints. They answer different needs. The dataset supports a new model or a fresh training recipe; an existing checkpoint supports a smaller, targeted continuation. Neither comes with a universal learning rate, sampling distribution, or guaranteed improvement.

### If starting with our dataset

We would treat the 401,975-pair export as a source of teaching examples, then design exposure around the intended workload. It is deduplicated and contains request text plus COMMAND JSON. It does not reproduce the earlier 474,635-presentation weighted epoch by itself. The public training recipes describe the models' lineage and settings, but the flat dataset does not include the full historical materialization and replay selection. Record the sampling used for a new run and deliberately choose the balance between task families.

Start by defining what the model should do: GNU/Linux versus another platform, ordinary interactive commands versus elaborate automation, and JSON versus plain command output. Our preference is the simplest command that preserves every requested constraint. A shorter command that loses recursion, case-insensitive matching, timestamps, or source/destination roles is still wrong.

Preserve literal command bytes, including whitespace, Unicode, leading hyphens, and quoted operands. Retokenize for the chosen model rather than reusing another model's cache. Check prompt masking, answer/EOS supervision, tokenizer round trips, truncation, and shuffled presentation order before spending money on training. If converting JSON answers to plain commands, extract the command value without rewriting it and change the system prompt and evaluation adapter consistently.

We would split evaluation by related task family and add separate probes for new wording, new operands, and new compositions. Two descriptions of the same command should not accidentally become evidence of unseen-task generalization merely because they landed in different random splits. Create a companion family manifest for those splits; the public file contains only request/response pairs and does not supply family labels or an official test set.

Use independently written tests and executable fixtures to challenge the intended behavior. Include adverse layouts that expose missing clauses, incorrect units, extra mutations, and incomplete output. ALFA and our existing panels have already influenced many decisions; a new project's final confirmation should include fresh, unconsulted material.

### If continuing one of our checkpoints

The public starting points are **1.5B supported A3** and **0.6B A1**. The older 1.5B parent, find-arm04 specialist, and 0.6B A3 remain research comparisons rather than the selected public releases. Their results illustrate why the choice should follow the intended workload and deployment precision: find-arm04 traded broad retention for ALFA, while 0.6B A3 retained more behavior in BF16 and had the better Q8 internal total.

For further training, load the public merged BF16 model and add a fresh adapter, or load its original cumulative adapter on the pinned upstream model. The second route requires the original base weights; [Hugging Face's PEFT checkpoint documentation](https://huggingface.co/docs/peft/main/en/developer_guides/checkpoint) explains that dependency. Applying the cumulative adapter to the already merged EC model would apply its learned change twice. The release cards show both loading paths. Neither includes the old optimizer, scheduler, or RNG state, so continuation starts a new optimization run.

For a continuation, we would first reproduce the starting model's scores with its saved prompt and native format. Then add a small, verified repair set mixed with representative old examples. In particular, explicitly verify that find replay includes find commands. Keep the starting weights available and measure individual gains and losses against them.

Our short 1.5B continuation results justify testing conservative rates such as **5e-6 and 1e-5**, with an early checkpoint around **100–200 updates**, before committing to a longer run. These are proposed starting experiments derived from our measured arms, not a proven optimum for every checkpoint. The fresh-HF full runs used different rates; continuation settings should not be copied blindly into training from scratch. We have no matched evidence establishing an advantage from restoring an old optimizer; record the optimization state used.

Evaluate the intended Q4 or Q8 artifact early, alongside full fidelity. A BF16 winner can lose after export, and a targeted benchmark win can conceal broad regressions. We would extend training only when the measured tradeoff remains useful, with the selection rule fixed before opening fresh confirmation.

### Which research bets we would prioritize

| Direction | Why it deserves a test | Confidence and missing evidence |
|---|---|---|
| Restore qualified old-find replay | The repair replay's blanket find exclusion was directly observed | High confidence in the omission; recovery after fixing it remains untested |
| Teach complete modifier, operand, and output distinctions | Actual failures drop case handling, confuse counts, swap roles, or omit requested output/state | Strong diagnosis; the right exposure for transfer and retention remains to be measured |
| Tune repair dose, learning rate, and horizon together | Short matched trials show learning and retention moving differently | Good support for small pilots; no universal full-run recipe |
| Improve 0.6B output robustness | Malformed JSON escaping accounted for concrete Q4 quoting losses | Worth a focused experiment; a decoder constraint would be an inference change, not a weight-training gain |
| Test verified preference pairs on the stronger checkpoints | Wrong model answers provide contrasts that positive SFT does not show explicitly | Exploratory: earlier 360M preference trials did not improve broad performance |
| Run a matched from-stock JSON/plain comparison | Our full plain run inherited JSON training and extra exposure | Useful for isolating format, with no current evidence promising a large improvement |

For preference training, correct and incorrect answers should be distinguished by qualified behavior checks, not by matching one preferred command string. [Direct Preference Optimization](https://arxiv.org/abs/2305.18290) provides one way to learn from answer preferences without an online rollout loop. We would compare it with matched positive-answer SFT on the same data and budget. Our prior negative results and numerical reference issue make that a research bet, not a recommended default.

Execution-reward reinforcement learning is a later possibility. First the verifier needs to reject convincing wrong outputs, unintended changes, and incomplete solutions across adverse fixtures. A model should not be rewarded for exploiting an environment defect or a weak grader. We have not demonstrated an RL improvement in this project.

### A practical next-experiment sequence

1. Reproduce the starting model and establish a compatible untouched-HF control if claiming a gain from training.
2. Build a small, independently verified repair/replay mixture and freeze development, retention, and fresh-confirmation splits.
3. Compare a few short training doses or rates on inexpensive hardware, retaining the actual command outputs and side effects.
4. Export the promising candidates to the intended deployment precision and inspect per-panel gains and losses.
5. Extend only the useful recipe; freeze the final selection before scoring fresh confirmation. Publish negative results and the exact prompt, checkpoint lineage, sampling, decoder, and grader definition.

We would avoid treating a larger corpus, smaller training loss, longer run, extra reasoning, or simpler output format as evidence of improvement by itself. The project's most reusable assets are the teaching examples, executable fixtures, preserved checkpoints, and failure map. They provide a concrete starting point for further work, while the remaining questions require new measurements.

## Public code, models, and evidence

- [ec source, installation, and inference](https://github.com/dirac-run/ec), [training guide](https://github.com/dirac-run/ec/blob/main/docs/TRAINING.md), and [measured model results](https://github.com/dirac-run/ec/blob/main/docs/RESULTS.md).
- [ALFA-updated source and setup](https://github.com/dirac-run/ALFA-updated), [change log](https://github.com/dirac-run/ALFA-updated/blob/main/CHANGELOG.md), [correctness boundaries](https://github.com/dirac-run/ALFA-updated/blob/main/REASONING.md), and [locked runtime assets](https://github.com/dirac-run/ALFA-updated/releases/tag/v0.1.0).
- [Published dataset and construction/privacy notes](https://huggingface.co/datasets/dirac-run/ec-training-data).
- [1.5B GGUF results and comparisons](https://huggingface.co/dirac-run/ec-1.5b-gguf), [0.6B per-quant results](https://huggingface.co/dirac-run/ec-0.6b-gguf), and trainable [1.5B](https://huggingface.co/dirac-run/ec-1.5b) / [0.6B](https://huggingface.co/dirac-run/ec-0.6b) releases.

The model repositories include machine-readable evaluation counts and source-summary hashes, exact inference/training profiles, and weight checksums. Later comparisons use ALFA grading-definition SHA256 `6f50e4fea37c125f73f93e5f785e95e91fda448b4bf347a9a2986db4d83b48dc`. Detailed historical experiment receipts and the full private chronological ledger remain archived locally; they are not all included in the public source repositories.
