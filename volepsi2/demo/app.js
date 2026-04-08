const stageNames = [
    "hash_mapping",
    "okvs_encoding",
    "vole_generation",
    "correlation_transfer",
    "intersection_calculation",
];

const defaultTrafficNote =
    "界面会明确区分实测流量和建模流量，避免把 benchmark 回退路径的估算值当成真实传输数据。";

const stageDictionary = {
    hash_mapping: {
        label: "哈希映射",
    },
    okvs_encoding: {
        label: "OKVS 编码",
    },
    vole_generation: {
        label: "VOLE 生成",
    },
    correlation_transfer: {
        label: "校正传输",
    },
    intersection_calculation: {
        label: "交集计算",
    },
};

const form = document.querySelector("#run-form");
const runButton = document.querySelector("#run-button");
const runMessage = document.querySelector("#run-message");
const statusPill = document.querySelector("#status-pill");
const stageFeed = document.querySelector("#stage-feed");
const timeChart = document.querySelector("#time-chart");
const trafficChart = document.querySelector("#traffic-chart");
const intersectionPreview = document.querySelector("#intersection-preview");
const receiverPreview = document.querySelector("#receiver-preview");
const senderPreview = document.querySelector("#sender-preview");
const trafficNote = document.querySelector("#traffic-note");
const presetChips = Array.from(document.querySelectorAll(".preset-chip"));
const modeChips = Array.from(document.querySelectorAll(".mode-chip"));
const syntheticControls = document.querySelector("#synthetic-controls");
const customControls = document.querySelector("#custom-controls");

const receiverSizeField = document.querySelector("#receiver-size");
const senderSizeField = document.querySelector("#sender-size");
const intersectionSizeField = document.querySelector("#intersection-size");
const numThreadsField = document.querySelector("#num-threads");
const binSizeHintField = document.querySelector("#bin-size-hint");
const seedField = document.querySelector("#seed");
const receiverTextField = document.querySelector("#receiver-text");
const senderTextField = document.querySelector("#sender-text");
const receiverFileField = document.querySelector("#receiver-file");
const senderFileField = document.querySelector("#sender-file");

const metricTotalTime = document.querySelector("#metric-total-time");
const metricTotalTraffic = document.querySelector("#metric-total-traffic");
const metricOkvsSize = document.querySelector("#metric-okvs-size");
const metricIntersectionSize = document.querySelector("#metric-intersection-size");
const metricVoleMode = document.querySelector("#metric-vole-mode");
const metricClustering = document.querySelector("#metric-clustering");
const metricProgress = document.querySelector("#metric-progress");
const metricSeedMode = document.querySelector("#metric-seed-mode");

const presetConfigs = {
    quick: {
        name: "快速演示",
        receiverSize: "4096",
        senderSize: "4096",
        intersectionSize: "512",
        numThreads: "4",
        binSizeHint: "2048",
        seed: "8241975136114742642",
    },
    balanced: {
        name: "均衡中型",
        receiverSize: "16384",
        senderSize: "16384",
        intersectionSize: "4096",
        numThreads: "4",
        binSizeHint: "8192",
        seed: "5577006791947779410",
    },
    throughput: {
        name: "高吞吐",
        receiverSize: "65536",
        senderSize: "65536",
        intersectionSize: "16384",
        numThreads: "8",
        binSizeHint: "16384",
        seed: "1357913579135791357",
    },
    million: {
        name: "百万级",
        receiverSize: "1048576",
        senderSize: "1048576",
        intersectionSize: "65536",
        numThreads: "16",
        binSizeHint: "32768",
        seed: "2468024680246802468",
    },
};

let currentSource = null;
let runCompleted = false;
let historyStepCounter = 0;
let selectedPresetKey = "quick";
let currentMode = "synthetic";

function formatMs(value) {
    if (!Number.isFinite(value)) {
        return "0 毫秒";
    }
    if (value >= 1000) {
        return `${(value / 1000).toFixed(2)} 秒`;
    }
    if (value >= 1) {
        return `${value.toFixed(2)} 毫秒`;
    }
    return `${(value * 1000).toFixed(1)} 微秒`;
}

function formatBytes(value) {
    const units = ["B", "KB", "MB", "GB"];
    let size = Number(value || 0);
    let index = 0;
    while (size >= 1024 && index < units.length - 1) {
        size /= 1024;
        index += 1;
    }
    return `${size.toFixed(size >= 10 || index === 0 ? 0 : 1)} ${units[index]}`;
}

function setStatus(kind, text) {
    statusPill.className = `status-pill ${kind}`;
    statusPill.textContent = text;
}

function nowLabel() {
    return new Intl.DateTimeFormat("zh-CN", {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
        hour12: false,
    }).format(new Date());
}

function clearHistory() {
    historyStepCounter = 0;
    runMessage.innerHTML = `
        <div class="run-message-line is-empty">
            点击按钮后，这里会在当前面板内按行记录求交过程。
        </div>`;
}

function pushHistory(title, detail) {
    const emptyRow = runMessage.querySelector(".is-empty");
    if (emptyRow) {
        emptyRow.remove();
    }

    historyStepCounter += 1;

    const item = document.createElement("div");
    item.className = "run-message-line";

    const time = document.createElement("span");
    time.className = "run-message-time";
    time.textContent = nowLabel();

    const text = document.createElement("div");
    text.className = "run-message-text";
    text.textContent = `${historyStepCounter}. ${title}：${detail}`;

    item.append(time, text);
    runMessage.appendChild(item);
}

function localizeStage(stage) {
    const localized = stageDictionary[stage.id];
    if (!localized) {
        return stage;
    }
    return {
        ...stage,
        label: localized.label,
    };
}

function localizedStageDetail(stage, telemetry) {
    switch (stage.id) {
        case "hash_mapping":
            return "将接收方和发送方元素映射到 GF(2^128) 基域。";
        case "okvs_encoding":
            return "将接收方集合编码为 OKVS 表 P。";
        case "vole_generation":
            return telemetry.usedRealVole
                ? "本次运行使用真实 silent VOLE，本阶段记录其本地 socket 传输字节。"
                : "本次运行使用模拟 VOLE 回退路径，本阶段没有真实网络传输字节。";
        case "correlation_transfer":
            return telemetry.usedModeledTransfers
                ? "当前为 benchmark/内存路径：该阶段只建模校正向量流量，并未执行真实传输。"
                : "接收方通过本地 socket 真实发送校正向量，并完成两侧 OKVS 解码。";
        case "intersection_calculation":
            return telemetry.usedModeledTransfers
                ? "当前为 benchmark/内存路径：该阶段只建模发送方标签流量，并未执行真实传输。"
                : "发送方通过本地 socket 真实发送标签集合，并在接收方侧完成交集匹配。";
        default:
            return stage.detail;
    }
}

function buildTrafficNote(telemetry, fallbackText = defaultTrafficNote) {
    if (!telemetry) {
        return fallbackText;
    }

    if (telemetry.usedModeledTransfers) {
        return telemetry.usedRealVole
            ? "当前结果来自 benchmark/内存路径：VOLE 字节为本地实测，但校正向量和标签集合字节为建模值，二者已分开统计。"
            : "当前结果来自 benchmark/内存路径：VOLE 使用模拟回退，校正向量和标签集合字节为建模值，不应视为真实传输测量。";
    }

    return telemetry.usedRealVole
        ? "当前结果来自本地 demo 路径：VOLE、校正向量和标签集合都通过本地 socket 实测。该结果用于演示，不代表跨主机部署测量。"
        : "当前结果来自本地 demo 路径：校正向量和标签集合通过本地 socket 实测，但 VOLE 使用模拟回退路径，该阶段没有真实传输字节。";
}

function formatTrafficSummary(telemetry) {
    const measured = formatBytes(telemetry.totalNetworkBytes);
    const modeled = Number(telemetry.totalEstimatedNetworkBytes || 0);
    if (modeled === 0) {
        return measured;
    }
    return `实测 ${measured} / 建模 ${formatBytes(modeled)}`;
}

function trafficModeLabel(stage) {
    return stage.networkBytesEstimated ? "建模" : "实测";
}

function formatInteger(value) {
    return Number(value).toLocaleString("zh-CN");
}

function getSeedLabel() {
    return seedField.options[seedField.selectedIndex]?.textContent || seedField.value;
}

function seedModeLabel(usedDeterministicSeed) {
    return usedDeterministicSeed ? "固定种子 / 可复现" : "系统随机";
}

function currentPresetName() {
    return presetConfigs[selectedPresetKey]?.name || "自定义组合";
}

function setDatasetMode(mode) {
    currentMode = mode === "custom" ? "custom" : "synthetic";
    modeChips.forEach((chip) => {
        chip.classList.toggle("is-active", chip.dataset.mode === currentMode);
    });
    syntheticControls.classList.toggle("is-hidden", currentMode !== "synthetic");
    customControls.classList.toggle("is-hidden", currentMode !== "custom");
}

function customTextHasItems(text) {
    return text
        .split(/[\n\r,;\t]/)
        .some((item) => item.trim().length > 0);
}

async function fillTextareaFromFile(fileInput, textField) {
    const file = fileInput.files?.[0];
    if (!file) {
        return;
    }
    textField.value = await file.text();
}

function syncPresetSelectionFromFields() {
    const payload = payloadFromForm();
    const matchedEntry = Object.entries(presetConfigs).find(([, preset]) =>
        preset.receiverSize === payload.receiverSize &&
        preset.senderSize === payload.senderSize &&
        preset.intersectionSize === payload.intersectionSize &&
        preset.numThreads === payload.numThreads &&
        preset.binSizeHint === payload.binSizeHint &&
        preset.seed === payload.seed);

    selectedPresetKey = matchedEntry ? matchedEntry[0] : "";
    presetChips.forEach((chip) => {
        chip.classList.toggle("is-active", chip.dataset.preset === selectedPresetKey);
    });
}

function applyPreset(presetKey) {
    const preset = presetConfigs[presetKey];
    if (!preset) {
        return;
    }

    selectedPresetKey = presetKey;
    receiverSizeField.value = preset.receiverSize;
    senderSizeField.value = preset.senderSize;
    intersectionSizeField.value = preset.intersectionSize;
    numThreadsField.value = preset.numThreads;
    binSizeHintField.value = preset.binSizeHint;
    seedField.value = preset.seed;

    presetChips.forEach((chip) => {
        chip.classList.toggle("is-active", chip.dataset.preset === presetKey);
    });
}

function resetDashboard() {
    metricTotalTime.textContent = "0 毫秒";
    metricTotalTraffic.textContent = "0 B";
    metricOkvsSize.textContent = "0";
    metricIntersectionSize.textContent = "0";
    metricVoleMode.textContent = "尚未开始";
    metricClustering.textContent = "尚未开始";
    metricProgress.textContent = `0 / ${stageNames.length} 个阶段`;
    metricSeedMode.textContent = "尚未开始";

    stageFeed.className = "stage-feed empty-state";
    stageFeed.textContent = "协议推进后，阶段更新会显示在这里。";

    timeChart.className = "bar-chart empty-state";
    timeChart.textContent = "运行演示后将在这里绘制各阶段耗时。";

    trafficChart.className = "bar-chart empty-state";
    trafficChart.textContent = "这里会区分展示各协议阶段的实测流量与建模流量。";

    intersectionPreview.className = "token-cloud empty-state";
    intersectionPreview.textContent = "完成运行后，这里会展示交集样本。";

    receiverPreview.innerHTML = '<li class="empty-row">暂无数据。</li>';
    senderPreview.innerHTML = '<li class="empty-row">暂无数据。</li>';
    trafficNote.textContent = defaultTrafficNote;
    clearHistory();
}

function validatePayload(payload) {
    const receiverSize = Number(payload.receiverSize);
    const senderSize = Number(payload.senderSize);
    const intersectionSize = Number(payload.intersectionSize);
    if (receiverSize < 0 || senderSize < 0) {
        return "集合规模必须为非负数。";
    }
    if (intersectionSize < 0) {
        return "交集规模必须为非负数。";
    }
    if (intersectionSize > Math.min(receiverSize, senderSize)) {
        return "交集规模不能超过任意一方集合规模。";
    }
    if (Number(payload.numThreads) < 1) {
        return "线程数必须大于 0。";
    }
    return "";
}

function validateCustomInputs() {
    if (!customTextHasItems(receiverTextField.value) && !customTextHasItems(senderTextField.value)) {
        return "至少需要提供一侧的有效数据项，不能两侧都为空。";
    }
    return "";
}

function payloadFromForm() {
    return {
        receiverSize: receiverSizeField.value,
        senderSize: senderSizeField.value,
        intersectionSize: intersectionSizeField.value,
        numThreads: numThreadsField.value,
        binSizeHint: binSizeHintField.value,
        seed: seedField.value,
    };
}

function renderBars(container, stages, valueKey, formatter, signalChart = false) {
    container.className = "bar-chart";
    container.innerHTML = "";

    if (!stages.length) {
        container.classList.add("empty-state");
        container.textContent = signalChart
            ? "这里会区分展示各协议阶段的实测流量与建模流量。"
            : "运行演示后将在这里绘制各阶段耗时。";
        return;
    }

    const maxValue = Math.max(...stages.map((stage) => Number(stage[valueKey]) || 0), 1);
    for (const stage of stages) {
        const row = document.createElement("div");
        row.className = signalChart
            ? `bar-row signal ${stage.networkBytesEstimated ? "modeled" : "measured"}`
            : "bar-row";

        const label = document.createElement("div");
        label.className = "bar-label";
        label.textContent = stage.label;

        const track = document.createElement("div");
        track.className = "bar-track";

        const fill = document.createElement("div");
        fill.className = "bar-fill";
        fill.style.width = `${((Number(stage[valueKey]) || 0) / maxValue) * 100}%`;
        track.appendChild(fill);

        const value = document.createElement("div");
        value.className = "bar-value";
        value.textContent = signalChart
            ? `${trafficModeLabel(stage)} ${formatter(Number(stage[valueKey]) || 0)}`
            : formatter(Number(stage[valueKey]) || 0);

        row.append(label, track, value);
        container.appendChild(row);
    }
}

function renderStageFeed(stages) {
    if (!stages.length) {
        stageFeed.className = "stage-feed empty-state";
        stageFeed.textContent = "协议推进后，阶段更新会显示在这里。";
        return;
    }

    stageFeed.className = "stage-feed";
    stageFeed.innerHTML = "";

    for (const stage of stages) {
        const card = document.createElement("article");
        card.className = "stage-card";

        const header = document.createElement("header");
        const title = document.createElement("h3");
        title.textContent = stage.label;

        const timing = document.createElement("span");
        timing.className = "pill";
        timing.textContent = formatMs(stage.durationMs);

        header.append(title, timing);

        const detail = document.createElement("p");
        detail.textContent = stage.detail;

        const meta = document.createElement("div");
        meta.className = "stage-meta";

        const traffic = document.createElement("span");
        traffic.className = `pill ${stage.networkBytesEstimated ? "signal" : "measured"}`;
        traffic.textContent = `${trafficModeLabel(stage)} ${formatBytes(stage.networkBytes)}`;

        meta.appendChild(traffic);

        card.append(header, detail, meta);
        stageFeed.appendChild(card);
    }
}

function renderPreviewList(element, items) {
    element.innerHTML = "";
    if (!items.length) {
        element.innerHTML = '<li class="empty-row">暂无数据。</li>';
        return;
    }
    for (const item of items) {
        const row = document.createElement("li");
        row.textContent = item;
        element.appendChild(row);
    }
}

function renderIntersection(items, indices) {
    intersectionPreview.className = "token-cloud";
    intersectionPreview.innerHTML = "";

    if (!items.length) {
        intersectionPreview.classList.add("empty-state");
        intersectionPreview.textContent = "本次运行没有可展示的交集样本。";
        return;
    }

    items.forEach((item, index) => {
        const token = document.createElement("span");
        token.className = "token";
        const itemIndex = indices[index];
        token.textContent = Number.isInteger(itemIndex) ? `[${itemIndex}] ${item}` : item;
        intersectionPreview.appendChild(token);
    });
}

function renderTelemetry(telemetry) {
    const localizedStages = (telemetry.stages || []).map((stage) => ({
        ...localizeStage(stage),
        detail: localizedStageDetail(stage, telemetry),
    }));

    metricTotalTime.textContent = formatMs(telemetry.totalDurationMs);
    metricTotalTraffic.textContent = formatTrafficSummary(telemetry);
    metricOkvsSize.textContent = telemetry.okvsSize.toLocaleString();
    metricIntersectionSize.textContent = telemetry.intersectionSize.toLocaleString();
    metricVoleMode.textContent = telemetry.usedRealVole ? "真实 silent VOLE" : "模拟回退路径";
    metricClustering.textContent = telemetry.usedClustering ? "已启用" : "未启用";
    metricProgress.textContent = `${localizedStages.length} / ${stageNames.length} 个阶段`;
    metricSeedMode.textContent = seedModeLabel(Boolean(telemetry.usedDeterministicSeed));

    renderStageFeed(localizedStages);
    renderBars(timeChart, localizedStages, "durationMs", formatMs, false);
    renderBars(trafficChart, localizedStages, "networkBytes", formatBytes, true);
    trafficNote.textContent = buildTrafficNote(telemetry);
}

function renderResult(payload) {
    renderTelemetry(payload.telemetry);
    metricIntersectionSize.textContent = payload.actualIntersectionSize.toLocaleString();
    renderIntersection(payload.intersectionPreview || [], payload.intersectionIndexPreview || []);
    renderPreviewList(receiverPreview, payload.receiverPreview || []);
    renderPreviewList(senderPreview, payload.senderPreview || []);
    trafficNote.textContent = payload.trafficNote || buildTrafficNote(payload.telemetry);
}

function closeStream() {
    if (currentSource) {
        currentSource.close();
        currentSource = null;
    }
}

async function startRun() {
    const payload = payloadFromForm();
    const validationError = currentMode === "custom"
        ? validateCustomInputs()
        : validatePayload(payload);
    if (validationError) {
        setStatus("error", "输入无效");
        pushHistory("参数校验失败", validationError);
        return;
    }

    closeStream();
    resetDashboard();

    runCompleted = false;
    runButton.disabled = true;
    setStatus("running", "连接中");

    let streamUrl = "";
    if (currentMode === "custom") {
        pushHistory(
            "数据预处理中",
            `正在上传并整理你提供的真实数据集，线程数 ${payload.numThreads}，聚类阈值 ${formatInteger(payload.binSizeHint)}，随机种子 ${getSeedLabel()}。`);

        try {
            const sessionBody = new URLSearchParams({
                receiverText: receiverTextField.value,
                senderText: senderTextField.value,
            });
            const response = await fetch("/api/session", {
                method: "POST",
                body: sessionBody,
            });
            const responseText = await response.text();
            if (!response.ok) {
                throw new Error(responseText.trim() || "数据预处理失败。");
            }

            const session = JSON.parse(responseText);
            pushHistory(
                "数据预处理完成",
                `接收方 ${formatInteger(session.receiverRawCount)} 条输入整理为 ${formatInteger(session.receiverSize)} 个唯一项，发送方 ${formatInteger(session.senderRawCount)} 条输入整理为 ${formatInteger(session.senderSize)} 个唯一项，预处理交集规模 ${formatInteger(session.intersectionSize)}。`);

            const params = new URLSearchParams({
                token: session.token,
                numThreads: payload.numThreads,
                binSizeHint: payload.binSizeHint,
                seed: payload.seed,
            });
            streamUrl = `/api/run?${params.toString()}`;
        } catch (error) {
            setStatus("error", "准备失败");
            pushHistory("数据预处理失败", error instanceof Error ? error.message : "未知错误。");
            runButton.disabled = false;
            return;
        }
    } else {
        pushHistory(
            "运行已创建",
            `使用“${currentPresetName()}”预设：接收方 ${formatInteger(payload.receiverSize)} 项，发送方 ${formatInteger(payload.senderSize)} 项，交集 ${formatInteger(payload.intersectionSize)} 项，线程数 ${payload.numThreads}，聚类阈值 ${formatInteger(payload.binSizeHint)}，随机种子 ${getSeedLabel()}。`);
        const params = new URLSearchParams(payload);
        streamUrl = `/api/run?${params.toString()}`;
    }

    currentSource = new EventSource(streamUrl);

    currentSource.addEventListener("ready", (event) => {
        const data = JSON.parse(event.data);
        const datasetSummary = data.datasetSummary || {};
        trafficNote.textContent = data.trafficNote || defaultTrafficNote;
        setStatus("running", "已连接");
        metricSeedMode.textContent = seedModeLabel(data.usedDeterministicSeed !== false);
        if (datasetSummary.mode === "custom") {
            pushHistory(
                "自定义数据已装载",
                `接收方 ${Number(data.receiverSize).toLocaleString()} 个唯一项、发送方 ${Number(data.senderSize).toLocaleString()} 个唯一项已经进入协议流程。`);
        } else {
            pushHistory(
                "本地双方连接完成",
                `接收方 ${Number(data.receiverSize).toLocaleString()} 项、发送方 ${Number(data.senderSize).toLocaleString()} 项的本地执行通道已经建立，准备开始求交流程。`);
        }
    });

    currentSource.addEventListener("progress", (event) => {
        const data = JSON.parse(event.data);
        renderTelemetry(data.telemetry);
        trafficNote.textContent = data.trafficNote || buildTrafficNote(data.telemetry);
        if (data.latestStage) {
            const stage = {
                ...localizeStage(data.latestStage),
                detail: localizedStageDetail(data.latestStage, data.telemetry),
            };
            pushHistory(
                `${stage.label}完成`,
                `${stage.detail} 本阶段耗时 ${formatMs(stage.durationMs)}，阶段${trafficModeLabel(stage)}流量 ${formatBytes(stage.networkBytes)}。`);
        }
        setStatus("running", "执行中");
    });

    currentSource.addEventListener("result", (event) => {
        const data = JSON.parse(event.data);
        runCompleted = true;
        renderResult(data);
        setStatus("done", "已完成");
        pushHistory(
            "求交完成",
            `最终得到 ${Number(data.actualIntersectionSize).toLocaleString()} 个交集元素，OKVS 槽位数 ${Number(data.okvsSize).toLocaleString()}，总耗时 ${formatMs(data.telemetry.totalDurationMs)}，总流量 ${formatTrafficSummary(data.telemetry)}。`);
        runButton.disabled = false;
        closeStream();
    });

    currentSource.addEventListener("failed", (event) => {
        const data = JSON.parse(event.data);
        runCompleted = true;
        setStatus("error", "运行失败");
        pushHistory("运行失败", data.message);
        runButton.disabled = false;
        closeStream();
    });

    currentSource.onerror = () => {
        if (runCompleted) {
            return;
        }
        setStatus("error", "连接丢失");
        pushHistory("连接中断", "演示 SSE 连接意外关闭，当前运行未完整返回结果。");
        runButton.disabled = false;
        closeStream();
    };
}

for (const chip of presetChips) {
    chip.addEventListener("click", () => {
        applyPreset(chip.dataset.preset);
    });
}

for (const chip of modeChips) {
    chip.addEventListener("click", () => {
        setDatasetMode(chip.dataset.mode);
    });
}

for (const field of [
    receiverSizeField,
    senderSizeField,
    intersectionSizeField,
    numThreadsField,
    binSizeHintField,
    seedField,
]) {
    field.addEventListener("change", () => {
        syncPresetSelectionFromFields();
    });
}

receiverFileField.addEventListener("change", async () => {
    await fillTextareaFromFile(receiverFileField, receiverTextField);
});

senderFileField.addEventListener("change", async () => {
    await fillTextareaFromFile(senderFileField, senderTextField);
});

form.addEventListener("submit", (event) => {
    event.preventDefault();
    void startRun();
});

resetDashboard();
applyPreset("quick");
setDatasetMode("synthetic");
