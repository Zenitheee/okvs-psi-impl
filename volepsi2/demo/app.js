const stageNames = [
    "hash_mapping",
    "okvs_encoding",
    "vole_generation",
    "correlation_transfer",
    "intersection_calculation",
];

const localizedTrafficNote =
    "所有协议流量都来自真实本地 socket 传输，不再使用后两阶段的估算值。";

const stageDictionary = {
    hash_mapping: {
        label: "哈希映射",
        detail: "将接收方和发送方元素映射到 GF(2^128) 基域。",
    },
    okvs_encoding: {
        label: "OKVS 编码",
        detail: "将接收方集合编码为 OKVS 表 P。",
    },
    vole_generation: {
        label: "VOLE 生成",
        detail: "生成 silent VOLE 相关性，并记录本地 socket 实测流量。",
    },
    correlation_transfer: {
        label: "校正传输",
        detail: "接收方通过本地 socket 真实发送校正向量，并完成两侧 OKVS 解码。",
    },
    intersection_calculation: {
        label: "交集计算",
        detail: "发送方通过本地 socket 真实发送标签集合，并在接收方侧完成交集匹配。",
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

const receiverSizeField = document.querySelector("#receiver-size");
const senderSizeField = document.querySelector("#sender-size");
const intersectionSizeField = document.querySelector("#intersection-size");
const numThreadsField = document.querySelector("#num-threads");
const binSizeHintField = document.querySelector("#bin-size-hint");
const seedField = document.querySelector("#seed");

const metricTotalTime = document.querySelector("#metric-total-time");
const metricTotalTraffic = document.querySelector("#metric-total-traffic");
const metricOkvsSize = document.querySelector("#metric-okvs-size");
const metricIntersectionSize = document.querySelector("#metric-intersection-size");
const metricVoleMode = document.querySelector("#metric-vole-mode");
const metricClustering = document.querySelector("#metric-clustering");
const metricProgress = document.querySelector("#metric-progress");

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
        detail: localized.detail,
    };
}

function formatInteger(value) {
    return Number(value).toLocaleString("zh-CN");
}

function getSeedLabel() {
    return seedField.options[seedField.selectedIndex]?.textContent || seedField.value;
}

function currentPresetName() {
    return presetConfigs[selectedPresetKey]?.name || "自定义组合";
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

    stageFeed.className = "stage-feed empty-state";
    stageFeed.textContent = "协议推进后，阶段更新会显示在这里。";

    timeChart.className = "bar-chart empty-state";
    timeChart.textContent = "运行演示后将在这里绘制各阶段耗时。";

    trafficChart.className = "bar-chart empty-state";
    trafficChart.textContent = "这里会显示各协议阶段在本地 socket 上的真实流量。";

    intersectionPreview.className = "token-cloud empty-state";
    intersectionPreview.textContent = "完成运行后，这里会展示交集样本。";

    receiverPreview.innerHTML = '<li class="empty-row">暂无数据。</li>';
    senderPreview.innerHTML = '<li class="empty-row">暂无数据。</li>';
    trafficNote.textContent = localizedTrafficNote;
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
            ? "这里会显示各协议阶段在本地 socket 上的真实流量。"
            : "运行演示后将在这里绘制各阶段耗时。";
        return;
    }

    const maxValue = Math.max(...stages.map((stage) => Number(stage[valueKey]) || 0), 1);
    for (const stage of stages) {
        const row = document.createElement("div");
        row.className = signalChart ? "bar-row signal" : "bar-row";

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
        value.textContent = formatter(Number(stage[valueKey]) || 0);

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
        traffic.textContent = `${stage.networkBytesEstimated ? "估算" : "实测"} ${formatBytes(stage.networkBytes)}`;

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
    const localizedStages = (telemetry.stages || []).map(localizeStage);

    metricTotalTime.textContent = formatMs(telemetry.totalDurationMs);
    metricTotalTraffic.textContent = formatBytes(telemetry.totalNetworkBytes);
    metricOkvsSize.textContent = telemetry.okvsSize.toLocaleString();
    metricIntersectionSize.textContent = telemetry.intersectionSize.toLocaleString();
    metricVoleMode.textContent = telemetry.usedRealVole ? "真实 silent VOLE" : "模拟";
    metricClustering.textContent = telemetry.usedClustering ? "已启用" : "未启用";
    metricProgress.textContent = `${localizedStages.length} / ${stageNames.length} 个阶段`;

    renderStageFeed(localizedStages);
    renderBars(timeChart, localizedStages, "durationMs", formatMs, false);
    renderBars(trafficChart, localizedStages, "networkBytes", formatBytes, true);
}

function renderResult(payload) {
    renderTelemetry(payload.telemetry);
    metricIntersectionSize.textContent = payload.actualIntersectionSize.toLocaleString();
    renderIntersection(payload.intersectionPreview || [], payload.intersectionIndexPreview || []);
    renderPreviewList(receiverPreview, payload.receiverPreview || []);
    renderPreviewList(senderPreview, payload.senderPreview || []);
    trafficNote.textContent = localizedTrafficNote;
}

function closeStream() {
    if (currentSource) {
        currentSource.close();
        currentSource = null;
    }
}

function startRun() {
    const payload = payloadFromForm();
    const validationError = validatePayload(payload);
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
    pushHistory(
        "运行已创建",
        `使用“${currentPresetName()}”预设：接收方 ${formatInteger(payload.receiverSize)} 项，发送方 ${formatInteger(payload.senderSize)} 项，交集 ${formatInteger(payload.intersectionSize)} 项，线程数 ${payload.numThreads}，聚类阈值 ${formatInteger(payload.binSizeHint)}，随机种子 ${getSeedLabel()}。`);

    const params = new URLSearchParams(payload);
    currentSource = new EventSource(`/api/run?${params.toString()}`);

    currentSource.addEventListener("ready", (event) => {
        const data = JSON.parse(event.data);
        trafficNote.textContent = localizedTrafficNote;
        setStatus("running", "已连接");
        pushHistory(
            "本地双方连接完成",
            `接收方 ${Number(data.receiverSize).toLocaleString()} 项、发送方 ${Number(data.senderSize).toLocaleString()} 项的本地执行通道已经建立，准备开始求交流程。`);
    });

    currentSource.addEventListener("progress", (event) => {
        const data = JSON.parse(event.data);
        renderTelemetry(data.telemetry);
        if (data.latestStage) {
            const stage = localizeStage(data.latestStage);
            pushHistory(
                `${stage.label}完成`,
                `${stage.detail} 本阶段耗时 ${formatMs(stage.durationMs)}，阶段流量 ${formatBytes(stage.networkBytes)}。`);
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
            `最终得到 ${Number(data.actualIntersectionSize).toLocaleString()} 个交集元素，OKVS 槽位数 ${Number(data.okvsSize).toLocaleString()}，总耗时 ${formatMs(data.telemetry.totalDurationMs)}，总流量 ${formatBytes(data.telemetry.totalNetworkBytes)}。`);
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

form.addEventListener("submit", (event) => {
    event.preventDefault();
    startRun();
});

resetDashboard();
applyPreset("quick");
