    const queuedAutomaticParameterChanges = new Map();
    let activeAutomaticParameterRecalculation = null;

    function automaticChangeKey(item) {
      if (typeof ciduAutomaticRecalculation !== 'undefined' && ciduAutomaticRecalculation) {
        return ciduAutomaticRecalculation.canonicalKey(item && (item.label || item.name || item.key));
      }
      return normalizeParamKey(item && (item.label || item.name || item.key));
    }

    function clearQueuedAutomaticParameterRecalculation() {
      queuedAutomaticParameterChanges.clear();
    }

    function automaticCalculationSnapshot() {
      if (typeof ciduAutomaticRecalculation === 'undefined' || !ciduAutomaticRecalculation) return null;
      return ciduAutomaticRecalculation.snapshotFromBridge(
        typeof ciduParameterBridge !== 'undefined' ? ciduParameterBridge : null
      );
    }

    function dispatchAutomaticParameterRecalculation() {
      if (isBusy || !queuedAutomaticParameterChanges.size) return false;
      const reportedChanges = Array.from(queuedAutomaticParameterChanges.values());
      const sourceChanges = reportedChanges
        .map((item) => ({ label: String(item && item.label || '').trim(), value: String(item && item.value || '').trim() }))
        .filter((item) => item.label && item.value);
      if (!sourceChanges.length) {
        clearQueuedAutomaticParameterRecalculation();
        return false;
      }
      clearQueuedAutomaticParameterRecalculation();
      const assignmentLines = [];
      sourceChanges.forEach((item) => assignmentLines.push(...buildEquipmentResultOverrideLines(item.label, item.value)));
      const prompt = assignmentLines.length ? '开始计算\n\n' + assignmentLines.join('\n') : '';
      if (!prompt) return false;
      setPendingEquipmentResultOverrides(sourceChanges);
      setPendingParameterResultOverrides([]);

      isBusy = true;
      sendBtn.disabled = true;
      const conversation = saveMessage({ role: 'user', text: prompt, attachments: [] });
      if (typeof ciduParameterBridge !== 'undefined' && ciduParameterBridge) {
        ciduParameterBridge.beginRequest(prompt);
      }
      activeAutomaticParameterRecalculation = {
        before: automaticCalculationSnapshot(),
        sourceChanges,
        reportedChanges: reportedChanges.length ? reportedChanges : sourceChanges,
      };
      const displayPrompt = prompt.replace(/^\s*开始计算\s*/u, '确认修改并计算\n').trim();
      addMessage('user', displayPrompt || '确认修改并计算', { attachments: [] });
      setStatus('参数草稿已确认，正在分析公式依赖并统一计算...');
      const sent = sendHost({
        type: 'ask',
        prompt,
        conversation_id: conversation.ciduConversationId || '',
        automatic_recalculation: true,
      });
      if (!sent) {
        activeAutomaticParameterRecalculation = null;
        isBusy = false;
        sendBtn.disabled = false;
      }
      return sent;
    }

    function requestAutomaticParameterRecalculation(payload) {
      const changes = payload && Array.isArray(payload.changes) ? payload.changes : [];
      const trigger = payload && payload.trigger;
      const candidates = changes.length ? changes : (trigger ? [trigger] : []);
      candidates.forEach((item) => {
        const key = automaticChangeKey(item);
        if (key) queuedAutomaticParameterChanges.set(key, item);
      });
      if (!queuedAutomaticParameterChanges.size) return false;
      if (isBusy) {
        setStatus('已确认的参数修改正在排队，将在当前请求完成后统一计算。');
        return true;
      }
      return dispatchAutomaticParameterRecalculation();
    }

    function finishAutomaticParameterRecalculation() {
      const active = activeAutomaticParameterRecalculation;
      if (!active) return '';
      activeAutomaticParameterRecalculation = null;
      if (typeof markAutomaticParameterRecalculationApplied === 'function') {
        markAutomaticParameterRecalculationApplied(active.reportedChanges);
      }
      if (typeof ciduAutomaticRecalculation === 'undefined' || !ciduAutomaticRecalculation) {
        return '已按确认的参数草稿完成重新计算。';
      }
      const after = automaticCalculationSnapshot();
      const diff = ciduAutomaticRecalculation.compare(active.before, after, active.sourceChanges);
      diff.edited = ciduAutomaticRecalculation.normalizeChanges(active.reportedChanges);
      return ciduAutomaticRecalculation.summary(diff);
    }

    function flushQueuedAutomaticParameterRecalculation() {
      if (!isBusy && queuedAutomaticParameterChanges.size) {
        dispatchAutomaticParameterRecalculation();
      }
    }

    document.getElementById('attachBtn').addEventListener('click', () => {
      sendHost({ type: 'chooseFile' });
    });

    document.getElementById('clearFilesBtn').addEventListener('click', () => {
      sendHost({ type: 'clearFiles' });
    });

    document.getElementById('clearBtn').addEventListener('click', () => {
      promptInput.value = '';
      setStatus('');
    });

    // 底部"选中模型 / 生成模型"按钮已下线，统一由对话富卡片里的"生成模型"按钮触发。
    chat.addEventListener('click', (event) => {
      const button = event.target.closest('.copy-code-btn');
      if (!button) return;
      copyCodeBlock(button);
    });

    document.getElementById('historyBtn').addEventListener('click', () => {
      historyPanel.classList.toggle('open');
      if (historyPanel.classList.contains('open')) {
        requestDifyConversations();
      } else {
        renderHistory();
      }
    });

    document.getElementById('closeHistoryBtn').addEventListener('click', () => {
      historyPanel.classList.remove('open');
    });

    document.getElementById('newChatBtn').addEventListener('click', () => {
      if (isBusy) {
        setStatus('当前请求完成后再新建对话。', true);
        return;
      }
      createConversation();
      promptInput.value = '';
      setStatus('');
      historyPanel.classList.remove('open');
      promptInput.focus();
    });

    document.getElementById('settingsBtn').addEventListener('click', openSettings);
    knowledgeBtn.addEventListener('click', requestKnowledgeSync);
    document.getElementById('closeSettingsBtn').addEventListener('click', closeSettings);
    document.getElementById('cancelSettingsBtn').addEventListener('click', closeSettings);
    document.getElementById('saveSettingsBtn').addEventListener('click', saveSettings);
    settingsModal.addEventListener('click', (event) => {
      if (event.target === settingsModal) {
        closeSettings();
      }
    });

    document.getElementById('sendBtn').addEventListener('click', () => {
      const promptInputValue = promptInput.value.trim();
      if (!promptInputValue) {
        setStatus('请先输入选型需求。', true);
        return;
      }
      if (isBusy) {
        setStatus('已有请求正在执行，请稍等。', true);
        return;
      }
      if (typeof cancelScheduledAutomaticParameterRecalculation === 'function') {
        cancelScheduledAutomaticParameterRecalculation();
      }
      clearQueuedAutomaticParameterRecalculation();
      const prompt = appendEditedParametersToPrompt(promptInputValue);
      isBusy = true;
      sendBtn.disabled = true;
      // 把当前选中的附件快照到这一条用户消息里：发送时显示在气泡内，存进历史下次也能回放
      const attachmentsSnapshot = currentAttachments.slice();
      const conversation = saveMessage({ role: 'user', text: prompt, attachments: attachmentsSnapshot });
      if (typeof ciduParameterBridge !== 'undefined' && ciduParameterBridge) {
        ciduParameterBridge.beginRequest(prompt);
      }
      addMessage('user', prompt, { attachments: attachmentsSnapshot });
      promptInput.value = '';
      setStatus('正在发送...');
      sendHost({ type: 'ask', prompt, conversation_id: conversation.ciduConversationId || '' });
    });

    // 发送快捷键支持两种模式，点击底部提示可切换；
    // 'ctrl-enter'：Ctrl/Cmd+Enter 发送、Enter 换行；
    // 'enter'：Enter 发送、Shift+Enter 换行（Ctrl+Enter 也走换行）。
    const SHORTCUT_STORAGE_KEY = 'cidu.sendKeyMode';
    let sendKeyMode = (() => {
      try {
        const v = localStorage.getItem(SHORTCUT_STORAGE_KEY);
        return v === 'enter' ? 'enter' : 'ctrl-enter';
      } catch (_) { return 'ctrl-enter'; }
    })();
    const shortcutHintEl = document.getElementById('shortcutHint');
    const refreshShortcutHint = () => {
      if (!shortcutHintEl) return;
      shortcutHintEl.textContent = sendKeyMode === 'enter'
        ? 'Enter 发送，Shift + Enter 换行（右键切换）'
        : 'Ctrl + Enter 发送，Enter 换行（右键切换）';
    };
    refreshShortcutHint();
    if (shortcutHintEl) {
      // 右键切换：避免单击误触发，同时拦掉默认浏览器右键菜单。
      shortcutHintEl.addEventListener('contextmenu', (event) => {
        event.preventDefault();
        sendKeyMode = sendKeyMode === 'enter' ? 'ctrl-enter' : 'enter';
        try { localStorage.setItem(SHORTCUT_STORAGE_KEY, sendKeyMode); } catch (_) { /* ignore */ }
        refreshShortcutHint();
      });
    }

    promptInput.addEventListener('keydown', (event) => {
      if (event.key !== 'Enter' || event.isComposing) return;
      if (sendKeyMode === 'enter') {
        if (event.shiftKey || event.ctrlKey || event.metaKey) return;
        event.preventDefault();
        document.getElementById('sendBtn').click();
      } else if (event.ctrlKey || event.metaKey) {
        event.preventDefault();
        document.getElementById('sendBtn').click();
      }
    });

    if (host) {
      host.addEventListener('message', (event) => {
        const data = event.data || {};
        if (data.type === 'attachments') {
          renderAttachments(data.files || []);
          setStatus(data.files && data.files.length ? '附件已选择。' : '');
        }
        if (data.type === 'status') {
          setStatus(data.message || '');
        }
        if (data.type === 'difyConversations') {
          if (data.ok) {
            applyDifyConversations(data.payload || {});
          } else {
            historyLoading = false;
            renderHistory();
            setStatus(data.message || '历史会话加载失败，请检查智能体地址和 API Key。', true);
          }
        }
        if (data.type === 'difyConversationMessages') {
          if (data.ok) {
            applyDifyConversationMessages(data.payload || {}, data.conversation_id || '');
            setStatus('');
          } else {
            setStatus(data.message || '历史消息加载失败。', true);
          }
        }
        if (data.type === 'settings') {
          applySettings(data);
          const conversation = activeConversation();
          if (conversation && Array.isArray(conversation.messages) && conversation.messages.length) {
            renderConversationMessages(conversation);
          }
        }
        if (data.type === 'settingsSaved') {
          applySettings(data);
          closeSettings();
          saveSettingsBtn.disabled = false;
          setStatus('设置已保存。');
        }
        if (data.type === 'settingsError') {
          saveSettingsBtn.disabled = false;
          setStatus(data.message || '设置保存失败。', true);
        }
        if (data.type === 'knowledgeGraphUpdateStart') {
          setStatus(data.message || '正在更新知识图谱...');
          startKnowledgeProgress(data.message || '正在更新知识库和知识图谱...');
        }
        if (data.type === 'knowledgeGraphUpdateResult') {
          const message = data.message || (data.ok ? '知识库和知识图谱更新完成。' : '知识库和知识图谱更新失败。');
          setStatus(message, !data.ok);
          setKnowledgeSyncState(data.ok ? 'done' : 'failed', message);
        }
        if (data.type === 'streamStart') {
          startStream();
        }
        if (data.type === 'streamDelta') {
          appendStreamDelta(data.delta || '');
        }
        if (data.type === 'streamEnd') {
          endStream();
          isBusy = false;
          sendBtn.disabled = false;
        }
        if (data.type === 'result') {
          const payload = typeof applyPendingEquipmentResultOverridesToPayload === 'function'
            ? applyPendingEquipmentResultOverridesToPayload(data.payload || {})
            : (data.payload || {});
          data.payload = payload;
          if (typeof ciduParameterBridge !== 'undefined' && ciduParameterBridge) {
            const bridgeResult = payload && (payload.answer_markdown || payload.message)
              ? payload
              : Object.assign({}, payload || {}, { answer_markdown: activeStreamText || '' });
            ciduParameterBridge.completeRequest(bridgeResult);
          }
          const automaticSummary = finishAutomaticParameterRecalculation();
          const answerMarkdown = payload && payload.answer_markdown;
          if (answerMarkdown && replaceStreamWithAnswer(answerMarkdown, payload || {})) {
            updateConversationFromResult(payload || {}, { replaceLastAssistant: true });
            updateJiavuActions(normalizeResultSelection(payload || {}));
            rememberRequestedParameters(payload || {});
            setStatus(automaticSummary || '');
            isBusy = false;
            sendBtn.disabled = false;
            lastStreamHadAnswer = false;
            flushQueuedAutomaticParameterRecalculation();
            return;
          }
          const streamAnswerVisible = lastStreamHadAnswer || activeStreamBubble || activeStreamText.trim();
          if (streamAnswerVisible) {
            updateConversationFromResult(payload || {}, { replaceLastAssistant: true });
          } else {
            renderResult(payload || {});
            updateConversationFromResult(payload || {});
          }
          updateJiavuActions(normalizeResultSelection(payload || {}));
          rememberRequestedParameters(payload || {});
          if (!activeStreamBubble) {
            lastStreamHadAnswer = false;
          }
          setStatus(automaticSummary || '');
          isBusy = false;
          sendBtn.disabled = false;
          flushQueuedAutomaticParameterRecalculation();
        }
        if (data.type === 'jiavuActionResult') {
          if (data.action === 'generateModel' && chat) {
            // 佳鱼模型生成可能触发原生窗口切换；回来后重新确认计算表编辑监听仍然有效。
            enhanceEditableParameterTables(chat);
            flushQueuedAutomaticParameterRecalculation();
          }
          const suffix = data.ok && data.action === 'generateModel'
            ? ' 可继续修改参数并再次生成模型。'
            : '';
          setStatus((data.message || (data.ok ? '佳鱼操作完成。' : '佳鱼操作失败。')) + suffix, !data.ok);
        }
      });
      sendHost({ type: 'ready' });
      sendHost({ type: 'getSettings' });
    } else {
      setStatus('当前页面未运行在 WebView2 内。', true);
      setKnowledgeSyncState('failed', '当前页面未运行在 WebView2 内，无法启动知识更新。');
    }
    loadConversations();

