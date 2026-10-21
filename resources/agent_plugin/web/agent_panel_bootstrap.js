    const chat = document.getElementById('chat');
    let empty = document.getElementById('empty');
    const promptInput = document.getElementById('prompt');
    const statusEl = document.getElementById('status');
    const attachmentsEl = document.getElementById('attachments');
    const historyPanel = document.getElementById('historyPanel');
    const historyList = document.getElementById('historyList');
    const sendBtn = document.getElementById('sendBtn');
    const knowledgeBtn = document.getElementById('knowledgeBtn');
    const knowledgeProgressText = document.getElementById('knowledgeProgressText');
    const settingsModal = document.getElementById('settingsModal');
    const baseUrlInput = document.getElementById('baseUrlInput');
    const apiKeyInput = document.getElementById('apiKeyInput');
    const jiavuDbHostInput = document.getElementById('jiavuDbHostInput');
    const jiavuDbPortInput = document.getElementById('jiavuDbPortInput');
    const jiavuDbNameInput = document.getElementById('jiavuDbNameInput');
    const jiavuDbUserInput = document.getElementById('jiavuDbUserInput');
    const jiavuDbPasswordInput = document.getElementById('jiavuDbPasswordInput');
    const jiavuModelExcelDirInput = document.getElementById('jiavuModelExcelDirInput');
    const difySyncBaseUrlInput = document.getElementById('difySyncBaseUrlInput');
    const difyDatasetApiKeyInput = document.getElementById('difyDatasetApiKeyInput');
    const difyDatasetIdInput = document.getElementById('difyDatasetIdInput');
    const difyKnowledgeGraphIdInput = document.getElementById('difyKnowledgeGraphIdInput');
    const difyDatasetIndexingTechniqueInput = document.getElementById('difyDatasetIndexingTechniqueInput');
    const difyKnowledgeBaseDocumentNameInput = document.getElementById('difyKnowledgeBaseDocumentNameInput');
    const jyasmServiceBaseUrlInput = document.getElementById('jyasmServiceBaseUrlInput');
    const jyasmServiceTokenInput = document.getElementById('jyasmServiceTokenInput');
    const jyasmServicePublicBaseUrlInput = document.getElementById('jyasmServicePublicBaseUrlInput');
    const jyasmServiceTriggerRefreshInput = document.getElementById('jyasmServiceTriggerRefreshInput');
    const jyasmServiceTimeoutMsInput = document.getElementById('jyasmServiceTimeoutMsInput');
    const saveSettingsBtn = document.getElementById('saveSettingsBtn');

    // 给设置面板里所有 .password-toggle 按钮绑定眼睛切换显示/隐藏。
    document.querySelectorAll('.password-toggle').forEach((btn) => {
      btn.addEventListener('click', () => {
        const targetId = btn.getAttribute('data-target');
        const input = targetId ? document.getElementById(targetId) : null;
        if (!input) return;
        const showing = input.type === 'text';
        input.type = showing ? 'password' : 'text';
        btn.classList.toggle('is-shown', !showing);
      });
    });
    const host = window.chrome && window.chrome.webview;
    let conversations = [];
    let activeConversationId = '';
    let historyLoading = false;
    let activeStreamBubble = null;
    let activeStreamText = '';
    let lastStreamHadAnswer = false;
    let isBusy = false;
    let currentSelection = null;
    // 当前已选附件快照（来自 host 端 attachments 事件，发送时复制进消息体保存到历史里）
    let currentAttachments = [];
    let currentSettings = {};
    let settingsLoaded = false;
    let knowledgeSyncTimer = 0;
    let knowledgeSyncResetTimer = 0;
    let knowledgeSyncProgress = 0;
    const defaultKnowledgeTooltip =
      '从佳鱼数据库重新生成插件本地知识库和知识图谱；如已填写 Dify 同步配置，会同步到生产环境。';

    function sendHost(message) {
      if (!host) {
        setStatus('WebView2 消息通道未就绪。', true);
        return false;
      }
      host.postMessage(JSON.stringify(message));
      return true;
    }

    function setStatus(text, error = false) {
      statusEl.textContent = text || '';
      statusEl.classList.toggle('error', Boolean(error));
    }

    function setKnowledgeTooltip(text) {
      knowledgeBtn.title = text || '更新知识库和知识图谱';
      knowledgeBtn.setAttribute('data-tooltip', text || defaultKnowledgeTooltip);
    }

    function setKnowledgeProgress(value, label) {
      knowledgeSyncProgress = Math.max(0, Math.min(100, Number(value) || 0));
      knowledgeBtn.style.setProperty('--sync-progress', `${knowledgeSyncProgress}%`);
      knowledgeProgressText.textContent = label || (knowledgeSyncProgress ? `${Math.round(knowledgeSyncProgress)}%` : '');
    }

    function stopKnowledgeProgressTimer() {
      if (knowledgeSyncTimer) {
        window.clearInterval(knowledgeSyncTimer);
        knowledgeSyncTimer = 0;
      }
    }

    function setKnowledgeSyncState(state, message = '') {
      window.clearTimeout(knowledgeSyncResetTimer);
      knowledgeBtn.classList.toggle('syncing', state === 'syncing');
      knowledgeBtn.classList.toggle('done', state === 'done');
      knowledgeBtn.classList.toggle('failed', state === 'failed');
      knowledgeBtn.disabled = state === 'syncing';
      if (state === 'idle') {
        stopKnowledgeProgressTimer();
        setKnowledgeProgress(0, '');
        setKnowledgeTooltip(defaultKnowledgeTooltip);
        return;
      }
      if (state === 'syncing') {
        setKnowledgeTooltip(message || '正在更新知识库和知识图谱...');
        return;
      }
      stopKnowledgeProgressTimer();
      setKnowledgeProgress(state === 'done' ? 100 : Math.max(knowledgeSyncProgress, 12), state === 'done' ? '100' : '!');
      setKnowledgeTooltip(message || (state === 'done' ? '知识库和知识图谱更新完成。' : '知识库和知识图谱更新失败。'));
      knowledgeSyncResetTimer = window.setTimeout(() => setKnowledgeSyncState('idle'), state === 'done' ? 2600 : 7000);
    }

    function startKnowledgeProgress(message) {
      stopKnowledgeProgressTimer();
      setKnowledgeSyncState('syncing', message || '正在更新知识库和知识图谱...');
      setKnowledgeProgress(Math.max(knowledgeSyncProgress, 8));
      knowledgeSyncTimer = window.setInterval(() => {
        const next = knowledgeSyncProgress < 55
          ? knowledgeSyncProgress + 7
          : knowledgeSyncProgress < 82
            ? knowledgeSyncProgress + 3
            : knowledgeSyncProgress < 94
              ? knowledgeSyncProgress + 1
              : knowledgeSyncProgress;
        setKnowledgeProgress(next);
      }, 650);
    }

    function buildSettingsFromInputs() {
      return {
        base_url: baseUrlInput.value.trim(),
        api_key: apiKeyInput.value.trim(),
        jiavu_db_host: jiavuDbHostInput.value.trim(),
        jiavu_db_port: jiavuDbPortInput.value.trim(),
        jiavu_db_name: jiavuDbNameInput.value.trim(),
        jiavu_db_user: jiavuDbUserInput.value.trim(),
        jiavu_db_password: jiavuDbPasswordInput.value,
        jiavu_model_excel_dir: jiavuModelExcelDirInput.value.trim(),
        dify_sync_base_url: difySyncBaseUrlInput.value.trim(),
        dify_dataset_api_key: difyDatasetApiKeyInput.value.trim(),
        dify_dataset_id: difyDatasetIdInput.value.trim(),
        dify_knowledge_graph_id: difyKnowledgeGraphIdInput.value.trim(),
        dify_dataset_indexing_technique: difyDatasetIndexingTechniqueInput.value.trim() || 'economy',
        dify_knowledge_base_document_name:
          difyKnowledgeBaseDocumentNameInput.value.trim() || '静态配置',
        jyasm_service_base_url: jyasmServiceBaseUrlInput.value.trim(),
        jyasm_service_token: jyasmServiceTokenInput.value.trim(),
        jyasm_service_public_base_url: jyasmServicePublicBaseUrlInput.value.trim(),
        jyasm_service_trigger_refresh: jyasmServiceTriggerRefreshInput.value.trim(),
        jyasm_service_timeout_ms: jyasmServiceTimeoutMsInput.value.trim()
      };
    }

    function validateKnowledgeSyncSettings(settings) {
      const missing = [];
      if (!settings.jiavu_db_host) missing.push('佳鱼数据库地址');
      if (!settings.jiavu_db_user) missing.push('佳鱼数据库用户名');
      if (missing.length) {
        return `请先打开设置，补全：${missing.join('、')}。端口为空时默认 3306，库名为空时会自动识别。`;
      }
      const hasDifyField = Boolean(
        settings.dify_sync_base_url ||
        settings.dify_dataset_api_key ||
        settings.dify_dataset_id ||
        settings.dify_knowledge_graph_id
      );
      if (hasDifyField) {
        const difyMissing = [];
        if (!settings.dify_sync_base_url && !settings.base_url) {
          difyMissing.push('Dify 同步地址，格式如 http://192.168.10.32/v1');
        }
        if (!settings.dify_dataset_api_key) {
          difyMissing.push('Dataset API Key');
        }
        if (!settings.dify_dataset_id && !settings.dify_knowledge_graph_id) {
          difyMissing.push('知识库 ID 或知识图谱 ID 至少一个');
        }
        if (difyMissing.length) {
          return `Dify 同步配置不完整，请在设置中补全：${difyMissing.join('、')}。只想生成本地文件时，请清空所有 Dify 同步配置。`;
        }
      }
      return '';
    }

    function requestKnowledgeSync() {
      if (knowledgeBtn.disabled) return;
      const settings = Object.assign({}, currentSettings, buildSettingsFromInputs());
      const validation = settingsLoaded ? validateKnowledgeSyncSettings(settings) : '';
      if (validation) {
        setStatus(validation, true);
        setKnowledgeSyncState('failed', validation);
        return;
      }
      setStatus('正在更新知识库和知识图谱...');
      startKnowledgeProgress('正在连接佳鱼数据库并生成知识库/知识图谱...');
      if (!sendHost({ type: 'updateKnowledgeGraph' })) {
        setKnowledgeSyncState('failed', 'WebView2 消息通道未就绪，无法启动知识更新。');
      }
    }

    function openSettings() {
      settingsModal.classList.add('open');
      settingsModal.setAttribute('aria-hidden', 'false');
      saveSettingsBtn.disabled = false;
      historyPanel.classList.remove('open');
      sendHost({ type: 'getSettings' });
      window.setTimeout(() => baseUrlInput.focus(), 0);
    }

    function closeSettings() {
      settingsModal.classList.remove('open');
      settingsModal.setAttribute('aria-hidden', 'true');
    }

    function applySettings(settings) {
      settings = settings || {};
      settingsLoaded = true;
      currentSettings = Object.assign({}, currentSettings, settings || {});
      baseUrlInput.value = settings.base_url || '';
      apiKeyInput.value = settings.api_key || '';
      jiavuDbHostInput.value = settings.jiavu_db_host || '';
      jiavuDbPortInput.value = settings.jiavu_db_port || '';
      jiavuDbNameInput.value = settings.jiavu_db_name || '';
      jiavuDbUserInput.value = settings.jiavu_db_user || '';
      jiavuDbPasswordInput.value = settings.jiavu_db_password || '';
      jiavuModelExcelDirInput.value = settings.jiavu_model_excel_dir || '';
      difySyncBaseUrlInput.value = settings.dify_sync_base_url || '';
      difyDatasetApiKeyInput.value = settings.dify_dataset_api_key || '';
      difyDatasetIdInput.value = settings.dify_dataset_id || '';
      difyKnowledgeGraphIdInput.value = settings.dify_knowledge_graph_id || '';
      difyDatasetIndexingTechniqueInput.value = settings.dify_dataset_indexing_technique || 'economy';
      difyKnowledgeBaseDocumentNameInput.value =
        !settings.dify_knowledge_base_document_name ||
        settings.dify_knowledge_base_document_name === 'jiavu_knowledge_base.jsonl'
          ? '静态配置'
          : settings.dify_knowledge_base_document_name;
      jyasmServiceBaseUrlInput.value = settings.jyasm_service_base_url || '';
      jyasmServiceTokenInput.value = settings.jyasm_service_token || '';
      jyasmServicePublicBaseUrlInput.value = settings.jyasm_service_public_base_url || '';
      jyasmServiceTriggerRefreshInput.value = settings.jyasm_service_trigger_refresh || '';
      jyasmServiceTimeoutMsInput.value = settings.jyasm_service_timeout_ms || '';
    }

    function saveSettings() {
      const settings = buildSettingsFromInputs();
      saveSettingsBtn.disabled = true;
      setStatus('正在保存设置...');
      sendHost(Object.assign({
        type: 'saveSettings',
      }, settings));
    }

    function ensureChat() {
      if (empty && empty.parentElement) {
        empty.remove();
      }
      empty = null;
    }

    function storageWrite() {
      // 历史会话由 Dify 按 user 隔离保存，本地只保留当前窗口内的临时状态。
    }

    function newId() {
      return `local-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    }

    function activeConversation() {
      return conversations.find((item) => item.id === activeConversationId);
    }

    function timestampMs(value) {
      const timestamp = Number(value || Date.now());
      if (!Number.isFinite(timestamp) || timestamp <= 0) {
        return Date.now();
      }
      return timestamp < 1000000000000 ? timestamp * 1000 : timestamp;
    }

    function formatTime(value) {
      const date = new Date(timestampMs(value));
      const month = `${date.getMonth() + 1}`.padStart(2, '0');
      const day = `${date.getDate()}`.padStart(2, '0');
      const hour = `${date.getHours()}`.padStart(2, '0');
      const minute = `${date.getMinutes()}`.padStart(2, '0');
      return `${month}-${day} ${hour}:${minute}`;
    }

    function historyGroupKey(value) {
      const oneDay = 24 * 60 * 60 * 1000;
      const now = new Date();
      const todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
      const date = new Date(timestampMs(value));
      const itemStart = new Date(date.getFullYear(), date.getMonth(), date.getDate()).getTime();
      const diffDays = Math.floor((todayStart - itemStart) / oneDay);
      if (diffDays <= 0) return 'today';
      if (diffDays < 7) return 'seven';
      if (diffDays < 30) return 'thirty';
      return '';
    }

    function requestDifyConversations() {
      historyLoading = true;
      renderHistory();
      if (!sendHost({ type: 'listConversations' })) {
        historyLoading = false;
        renderHistory();
      }
    }

    function normalizeDifyConversation(item) {
      const id = String(item && item.id || '').trim();
      if (!id) return null;
      const updatedAt = item.updated_at || item.created_at || Date.now();
      const createdAt = item.created_at || updatedAt;
      return {
        id: `dify-${id}`,
        ciduConversationId: id,
        title: item.name || item.title || item.query || '历史会话',
        created_at: createdAt,
        updated_at: updatedAt,
        selection: null,
        messages: [],
        isRemote: true,
        loaded: false
      };
    }

    function upsertConversation(next) {
      if (!next || !next.id) return null;
      const index = conversations.findIndex((item) => {
        return item.id === next.id ||
          (item.ciduConversationId && next.ciduConversationId && item.ciduConversationId === next.ciduConversationId);
      });
      if (index >= 0) {
        const previous = conversations[index];
        conversations[index] = Object.assign({}, previous, next, {
          messages: next.messages && next.messages.length ? next.messages : (previous.messages || []),
          selection: next.selection || previous.selection || null,
          loaded: Boolean(next.loaded || previous.loaded)
        });
        return conversations[index];
      }
      conversations.push(next);
      return next;
    }

    function applyDifyConversations(payload) {
      historyLoading = false;
      const items = Array.isArray(payload && payload.data) ? payload.data : [];
      const active = activeConversation();
      const keepActiveDraft = active && !active.isRemote && Array.isArray(active.messages) && active.messages.length
        ? active
        : null;
      const nextConversations = [];
      const previous = conversations.slice();
      items.map(normalizeDifyConversation).filter(Boolean).forEach((remote) => {
        const old = previous.find((item) => item.ciduConversationId && item.ciduConversationId === remote.ciduConversationId);
        nextConversations.push(Object.assign({}, old || {}, remote, {
          messages: old && Array.isArray(old.messages) ? old.messages : [],
          selection: old && old.selection ? old.selection : null,
          loaded: Boolean(old && old.loaded),
          title: remote.title || (old && old.title) || '历史会话',
          updated_at: remote.updated_at || (old && old.updated_at) || Date.now(),
          isRemote: true
        }));
      });
      conversations = nextConversations;
      if (keepActiveDraft) {
        upsertConversation(keepActiveDraft);
      }
      conversations.sort((a, b) => Number(b.updated_at || 0) - Number(a.updated_at || 0));
      renderHistory();
    }

    function answerFromDifyMessage(item) {
      if (!item) return '';
      if (typeof item.answer === 'string') return item.answer;
      if (item.outputs && typeof item.outputs.answer === 'string') return item.outputs.answer;
      if (item.outputs && typeof item.outputs.text === 'string') return item.outputs.text;
      return '';
    }

    function applyDifyConversationMessages(payload, conversationId) {
      const items = Array.isArray(payload && payload.data) ? payload.data : [];
      const id = String(conversationId || (items[0] && items[0].conversation_id) || '').trim();
      let conversation = conversations.find((item) => item.ciduConversationId === id);
      if (!conversation && id) {
        conversation = upsertConversation({
          id: `dify-${id}`,
          ciduConversationId: id,
          title: '历史会话',
          created_at: Date.now(),
          updated_at: Date.now(),
          selection: null,
          messages: [],
          isRemote: true,
          loaded: false
        });
      }
      if (!conversation) return;
      const messages = [];
      items.forEach((item) => {
        const query = item.query || item.inputs && item.inputs.query || '';
        if (query) {
          messages.push({ role: 'user', text: query });
        }
        const answer = answerFromDifyMessage(item);
        if (answer) {
          messages.push({
            role: 'result',
            payload: {
              ok: true,
              message: answer,
              answer_markdown: answer,
              conversation_id: id || item.conversation_id || ''
            }
          });
        }
      });
      conversation.messages = messages;
      conversation.loaded = true;
      if (items[0] && items[0].query && (!conversation.title || conversation.title === '历史会话')) {
        conversation.title = String(items[0].query).slice(0, 24);
      }
      activeConversationId = conversation.id;
      renderConversationMessages(conversation);
      renderHistory();
    }

    function clearChat() {
      chat.innerHTML = '';
      const emptyState = document.createElement('div');
      emptyState.className = 'empty';
      emptyState.id = 'empty';
      emptyState.innerHTML = '<strong>描述你要选择的模型或系统</strong><span>我会返回选型模型、检索依据和所需参数。</span>';
      chat.appendChild(emptyState);
      empty = emptyState;
    }

    function renderConversationMessages(conversation) {
      chat.innerHTML = '';
      currentSelection = conversation && conversation.selection ? Object.assign({}, conversation.selection) : null;
      if (typeof ciduParameterBridge !== 'undefined' && ciduParameterBridge) {
        ciduParameterBridge.restoreLatestCalculation(
          conversation && Array.isArray(conversation.messages) ? conversation.messages : []
        );
      }
      updateJiavuActions(currentSelection, false);
      if (!conversation || !conversation.messages.length) {
        clearChat();
        return;
      }
      conversation.messages.forEach((message) => {
        if (message.role === 'result') {
          renderResult(message.payload || {});
        } else if (message.role === 'assistant' && message.payload) {
          renderResult(message.payload || {});
        } else if (message.role === 'assistant' && isSelectionResponse({ answer_markdown: message.text || '' })) {
          renderResult({ answer_markdown: message.text || '' });
        } else {
          addMessage(message.role, message.text || '', { attachments: message.attachments || [] });
        }
      });
    }

    function renderHistory() {
      historyList.innerHTML = '';
      const visibleConversations = conversations.filter((conversation) => {
        return conversation && (conversation.isRemote || (Array.isArray(conversation.messages) && conversation.messages.length));
      });
      if (historyLoading) {
        const loading = document.createElement('div');
        loading.className = 'history-empty';
        loading.textContent = '正在从 Dify 加载历史会话...';
        historyList.appendChild(loading);
      }
      if (!visibleConversations.length) {
        if (historyLoading) return;
        const emptyHistory = document.createElement('div');
        emptyHistory.className = 'history-empty';
        emptyHistory.textContent = '暂无历史会话。';
        historyList.appendChild(emptyHistory);
        return;
      }
      const groups = [
        { key: 'today', title: '今天', items: [] },
        { key: 'seven', title: '近 7 天', items: [] },
        { key: 'thirty', title: '近 30 天', items: [] }
      ];
      const groupMap = new Map(groups.map((group) => [group.key, group]));
      visibleConversations
        .slice()
        .sort((a, b) => timestampMs(b.updated_at) - timestampMs(a.updated_at))
        .forEach((conversation) => {
          const group = groupMap.get(historyGroupKey(conversation.updated_at));
          if (!group) return;
          group.items.push(conversation);
        });
      const visibleGroups = groups.filter((group) => group.items.length);
      if (!visibleGroups.length) {
        if (historyLoading) return;
        const emptyHistory = document.createElement('div');
        emptyHistory.className = 'history-empty';
        emptyHistory.textContent = '近 30 天内暂无历史会话。';
        historyList.appendChild(emptyHistory);
        return;
      }
      visibleGroups.forEach((group) => {
        const title = document.createElement('div');
        title.className = 'history-section-title';
        title.textContent = group.title;
        historyList.appendChild(title);
        group.items.forEach((conversation) => {
          const item = document.createElement('button');
          item.type = 'button';
          item.className = `history-item ${conversation.id === activeConversationId ? 'active' : ''}`;
          item.innerHTML = `<span class="history-title">${escapeHtml(conversation.title || '新对话')}</span><span class="history-meta">${formatTime(conversation.updated_at)}</span>`;
          item.addEventListener('click', () => {
            activeConversationId = conversation.id;
            if (conversation.isRemote && !conversation.loaded && conversation.ciduConversationId) {
              clearChat();
              setStatus('正在加载历史会话...');
              sendHost({ type: 'loadConversationMessages', conversation_id: conversation.ciduConversationId });
            } else {
              renderConversationMessages(conversation);
            }
            renderHistory();
            historyPanel.classList.remove('open');
          });
          historyList.appendChild(item);
        });
      });
    }

    function createConversation() {
      const conversation = {
        id: newId(),
        ciduConversationId: '',
        title: '新对话',
        created_at: Date.now(),
        updated_at: Date.now(),
        selection: null,
        messages: []
      };
      conversations.unshift(conversation);
      activeConversationId = conversation.id;
      renderHistory();
      clearChat();
      return conversation;
    }

    function ensureConversation() {
      return activeConversation() || createConversation();
    }

    function saveMessage(message) {
      const conversation = ensureConversation();
      conversation.messages.push(message);
      conversation.updated_at = Date.now();
      if (currentSelection) {
        conversation.selection = Object.assign({}, currentSelection);
      }
      if (message.role === 'user' && (!conversation.title || conversation.title === '新对话')) {
        conversation.title = (message.text || '新对话').slice(0, 24);
      }
      renderHistory();
      return conversation;
    }

    function updateConversationFromResult(result, options = {}) {
      const conversation = ensureConversation();
      if (result.conversation_id) {
        conversation.ciduConversationId = result.conversation_id;
      }
      if (currentSelection) {
        conversation.selection = Object.assign({}, currentSelection);
      }
      conversation.updated_at = Date.now();
      if (options.replaceLastAssistant) {
        let replaced = false;
        for (let index = conversation.messages.length - 1; index >= 0; index -= 1) {
          if (conversation.messages[index].role === 'assistant') {
            conversation.messages[index] = { role: 'result', payload: result };
            replaced = true;
            break;
          }
        }
        if (!replaced) {
          conversation.messages.push({ role: 'result', payload: result });
        }
      } else if (!options.skipMessage) {
        conversation.messages.push({ role: 'result', payload: result });
      }
      renderHistory();
    }

    function loadConversations() {
      conversations = [];
      activeConversationId = '';
      clearChat();
      renderHistory();
      if (host) {
        requestDifyConversations();
      }
    }

    function escapeHtml(text) {
      return String(text || '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
    }

    function normalizeMarkdownText(text) {
      const knownHeadings = new Map([
        ['选型结果', '## 选型结果'],
        ['选型依据', '### 选型依据'],
        ['模型需要的参数', '### 模型需要的参数'],
        ['候选模型', '### 候选模型'],
        ['识别到的检索线索', '### 识别到的检索线索']
      ]);
      const lines = String(text || '')
        .replace(/\r\n/g, '\n')
        .replace(/\n{3,}/g, '\n\n')
        .replace(/([^\n])(?=#{1,6}\s+)/g, '$1\n')
        .split('\n');
      const normalized = [];
      for (const line of lines) {
        const trimmed = line.trim();
        if (/^#{1,6}$/.test(trimmed)) {
          continue;
        }
        if (knownHeadings.has(trimmed)) {
          normalized.push(knownHeadings.get(trimmed));
          continue;
        }
        normalized.push(line);
      }
      return normalized.join('\n');
    }

    function sanitizeRenderedHtml(html) {
      const template = document.createElement('template');
      template.innerHTML = html;
      template.content.querySelectorAll('script, iframe, object, embed, link, meta, style').forEach((node) => node.remove());
      template.content.querySelectorAll('*').forEach((node) => {
        [...node.attributes].forEach((attribute) => {
          const name = attribute.name.toLowerCase();
          const value = attribute.value.trim().toLowerCase();
          if (name.startsWith('on') || value.startsWith('javascript:')) {
            node.removeAttribute(attribute.name);
          }
        });
      });
      return template.innerHTML;
    }

    function getMarkdownParser() {
      const candidate = window.marked;
      if (candidate && typeof candidate.parse === 'function') {
        return candidate.parse.bind(candidate);
      }
      if (candidate && typeof candidate.marked === 'function') {
        return candidate.marked.bind(candidate);
      }
      if (typeof candidate === 'function') {
        return candidate;
      }
      return null;
    }

    function renderMarkdownPlain(text) {
      const source = normalizeMarkdownText(text);
      const parser = getMarkdownParser();
      if (!parser) {
        return `<p>${escapeHtml(source).replace(/\n/g, '<br>')}</p>`;
      }
      const html = parser(source, { gfm: true, breaks: true, headerIds: false, mangle: false });
      return sanitizeRenderedHtml(html);
    }

    function renderThinkBlocks(text) {
      const source = String(text || '');
      const html = [];
      let cursor = 0;
      const pattern = /<think>([\s\S]*?)(<\/think>|$)/gi;
      let match;
      while ((match = pattern.exec(source)) !== null) {
        const before = source.slice(cursor, match.index);
        if (before) {
          html.push(renderMarkdownPlain(before));
        }
        const isComplete = Boolean(match[2]);
        const body = match[1] || '';
        const title = isComplete ? '思考过程' : '正在思考...';
        const openAttr = isComplete ? '' : ' open';
        html.push(
          `<details class="think-block"${openAttr}>` +
          `<summary>${title}</summary>` +
          `<div class="think-body">${renderMarkdownPlain(body)}</div>` +
          `</details>`
        );
        cursor = match.index + match[0].length;
      }
      const rest = source.slice(cursor);
      if (rest) {
        html.push(renderMarkdownPlain(rest));
      }
      return html.join('');
    }

    function stripCalculationProcessSection(markdown) {
      const lines = String(markdown || '').replace(/\r\n/g, '\n').split('\n');
      const output = [];
      let skipping = false;
      for (const line of lines) {
        const stripped = line.trim();
        if (/^#{1,6}\s*计算过程\s*$/.test(stripped)) {
          skipping = true;
          continue;
        }
        if (skipping && /^#{1,6}\s+\S/.test(stripped)) {
          skipping = false;
        }
        if (skipping) continue;
        output.push(line);
      }
      return output.join('\n').trim();
    }

    function renderMarkdown(text) {
      return renderThinkBlocks(stripCalculationProcessSection(text));
    }

    function isInlineParameterSectionTitle(line) {
      const title = stripMarkdown(line)
        .replace(/^#{1,6}\s*/, '')
        .replace(/[：:]\s*$/, '')
        .trim();
      return title === '参数说明' || title === '模型需要的参数' || title === '参数模板';
    }

    function isModelBlockHeadingLine(line) {
      return /^\s*(?:#{1,6}\s*)?(?:\*\*)?\s*模型\s*(?:\d+|[一二三四五六七八九十]+)\s*[：:]/.test(String(line || ''));
    }

    function stripInlineParameterSection(markdown) {
      const lines = String(markdown || '').replace(/\r\n/g, '\n').split('\n');
      const output = [];
      let skipping = false;
      let skippingFence = false;
      for (const line of lines) {
        const heading = line.match(/^\s*(#{1,6})\s+(.+?)\s*$/);
        const modelBlockHeading = isModelBlockHeadingLine(line);
        if (skipping && modelBlockHeading) {
          skipping = false;
          skippingFence = false;
          output.push(line);
          continue;
        }
        if (skipping) {
          if (/^\s*```/.test(line)) {
            skippingFence = !skippingFence;
            continue;
          }
          if (!skippingFence && heading && !isInlineParameterSectionTitle(line)) {
            skipping = false;
          } else {
            continue;
          }
        }
        if (isInlineParameterSectionTitle(line)) {
          skipping = true;
          skippingFence = false;
          continue;
        }
        if (heading) {
          const title = stripMarkdown(heading[2]);
          if (title === '参数说明' || title === '模型需要的参数') {
            skipping = true;
            skippingFence = false;
            continue;
          }
        }
        if (!skipping) {
          output.push(line);
        }
      }
      return output.join('\n').replace(/\n{3,}/g, '\n\n').trim();
    }

    function copyIconSvg() {
      return '<svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>';
    }

    function enhanceCodeBlocks(container) {
      if (!container) return;
      container.querySelectorAll('pre').forEach((pre) => {
        if (pre.closest('.code-block-wrap')) return;
        const code = pre.querySelector('code');
        if (!code) return;
        const wrap = document.createElement('div');
        wrap.className = 'code-block-wrap';
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'copy-code-btn';
        button.title = '复制代码';
        button.setAttribute('aria-label', '复制代码');
        button.innerHTML = copyIconSvg();
        pre.parentNode.insertBefore(wrap, pre);
        wrap.append(pre, button);
      });
    }

    function setMarkdownHtml(container, text, options) {
      container.innerHTML = renderMarkdown(text);
      enhanceCodeBlocks(container);
      if (!options || !options.skipEditableTables) {
        enhanceEditableParameterTables(container);
      }
      if (typeof rememberEquipmentSelectionFromText === 'function') {
        rememberEquipmentSelectionFromText(text, 'markdown-render');
      }
    }

    function copyTextFallback(text) {
      const textarea = document.createElement('textarea');
      textarea.value = text;
      textarea.setAttribute('readonly', '');
      textarea.style.position = 'fixed';
      textarea.style.top = '-1000px';
      textarea.style.opacity = '0';
      document.body.appendChild(textarea);
      textarea.select();
      let copied = false;
      try {
        copied = document.execCommand('copy');
      } catch (error) {
        copied = false;
      }
      textarea.remove();
      return copied;
    }

    async function copyCodeBlock(button) {
      const wrap = button.closest('.code-block-wrap');
      const code = wrap && wrap.querySelector('pre code');
      const text = code ? code.textContent : '';
      if (!text) return;
      let copied = false;
      try {
        if (navigator.clipboard && navigator.clipboard.writeText) {
          await navigator.clipboard.writeText(text);
          copied = true;
        }
      } catch (error) {
        copied = false;
      }
      if (!copied) {
        copied = copyTextFallback(text);
      }
      if (!copied) {
        setStatus('复制失败，请手动选择代码。', true);
        return;
      }
      const originalTitle = button.title;
      button.classList.add('copied');
      button.title = '已复制';
      button.setAttribute('aria-label', '已复制');
      setStatus('已复制代码。');
      window.clearTimeout(button.copyResetTimer);
      button.copyResetTimer = window.setTimeout(() => {
        button.classList.remove('copied');
        button.title = originalTitle || '复制代码';
        button.setAttribute('aria-label', '复制代码');
      }, 1200);
    }

    function addMessage(role, text, options) {
      ensureChat();
      const wrap = document.createElement('article');
      wrap.className = `message ${role}`;
      const roleEl = document.createElement('div');
      roleEl.className = 'role';
      roleEl.textContent = role === 'user' ? '你' : 'Cidu';
      const bubble = document.createElement('div');
      bubble.className = 'bubble markdown';
      const attachments = options && Array.isArray(options.attachments) ? options.attachments : null;
      if (attachments && attachments.length) {
        // 有附件时：先画文件卡片，再画 markdown 文本；保留旧调用方对 bubble 的直接 innerHTML 操作时的兼容路径走 else 分支
        const box = document.createElement('div');
        box.className = 'bubble-attachments';
        attachments.forEach((file) => box.appendChild(buildBubbleAttachment(file)));
        bubble.appendChild(box);
        if (text) {
          const textEl = document.createElement('div');
          textEl.className = 'bubble-text';
          setMarkdownHtml(textEl, text);
          bubble.appendChild(textEl);
        }
      } else {
        setMarkdownHtml(bubble, text);
      }
      wrap.append(roleEl, bubble);
      chat.appendChild(wrap);
      chat.scrollTop = chat.scrollHeight;
      return bubble;
    }

    function startStream() {
      activeStreamText = '';
      lastStreamHadAnswer = false;
      activeStreamBubble = addMessage('assistant', '');
      activeStreamBubble.innerHTML = '<p>正在选型...</p>';
    }

    function appendStreamDelta(delta) {
      if (!activeStreamBubble) {
        startStream();
      }
      activeStreamText += delta || '';
      setMarkdownHtml(activeStreamBubble, activeStreamText, { skipEditableTables: true });
      chat.scrollTop = chat.scrollHeight;
    }

