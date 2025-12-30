<script setup>
import { ref, onMounted, computed, nextTick } from 'vue'
import { useI18n } from 'vue-i18n'
import { useToast } from '../composables/useToast'
import { useConfirm } from '../composables/useConfirm'

const { t } = useI18n()
const { success, error } = useToast()
const { confirm } = useConfirm()

// 状态
const loading = ref(false)
const saving = ref(false)
const currentMode = ref(0) // 0=自动, 1=手动
const autoStart = ref(0)
const templates = ref([])
const selectedTemplateId = ref(0)
const showDialog = ref(false)
const isEditing = ref(false)
const currentTemplate = ref(null) // API返回的当前绑定模板（含应用状态）

// 表单
const form = ref({
  id: 0,
  name: '',
  apn: '',
  protocol: 'dual',
  username: '',
  password: '',
  auth_method: 'chap'
})

// 预设（使用计算属性以支持语言切换）
const presets = computed(() => [
  { name: t('apn.chinaMobile'), apn: 'cmnet', color: 'bg-blue-500' },
  { name: t('apn.chinaUnicom'), apn: '3gnet', color: 'bg-red-500' },
  { name: t('apn.chinaTelecom'), apn: 'ctnet', color: 'bg-green-500' },
  { name: t('apn.chinaBroadnet'), apn: 'cbnet', color: 'bg-purple-500' }
])

// 计算属性
const selectedTemplate = computed(() => 
  templates.value.find(t => t.id === selectedTemplateId.value)
)

// API请求头
function getHeaders(json = false) {
  const token = localStorage.getItem('auth_token')
  const headers = {}
  if (token) headers['Authorization'] = `Bearer ${token}`
  if (json) headers['Content-Type'] = 'application/json'
  return headers
}

// 获取配置
async function fetchConfig() {
  try {
    const res = await fetch('/api/apn/config', { headers: getHeaders() })
    if (!res.ok) return
    const data = await res.json()
    if (data.status === 'ok' && data.data) {
      currentMode.value = data.data.mode || 0
      selectedTemplateId.value = data.data.template_id || 0
      autoStart.value = data.data.auto_start || 0
      currentTemplate.value = data.data.template || null
    }
  } catch (e) {
    console.error('获取配置失败:', e)
  }
}

// 获取模板列表
async function fetchTemplates() {
  try {
    const res = await fetch('/api/apn/templates', { headers: getHeaders() })
    if (!res.ok) return
    const data = await res.json()
    if (data.status === 'ok' && data.data) {
      templates.value = data.data
    }
  } catch (e) {
    console.error('获取模板失败:', e)
  }
}

// 保存配置
async function saveConfig() {
  // 手动模式必须选择模板
  if (currentMode.value === 1 && selectedTemplateId.value <= 0) {
    error(t('apn.selectTemplateFirst'))
    return
  }
  
  saving.value = true
  try {
    // 保存配置
    const res = await fetch('/api/apn/config', {
      method: 'POST',
      headers: getHeaders(true),
      body: JSON.stringify({
        mode: currentMode.value,
        template_id: currentMode.value === 1 ? selectedTemplateId.value : 0,
        auto_start: currentMode.value === 1 ? autoStart.value : 0
      })
    })
    if (!res.ok) throw new Error('保存失败')
    
    // 自动模式：清除APN
    if (currentMode.value === 0) {
      const clearRes = await fetch('/api/apn/clear', { method: 'POST', headers: getHeaders(true) })
      if (clearRes.ok) {
        success(t('apn.apnCleared') || 'APN已清除，使用系统自动配置')
      } else {
        console.warn('清除APN失败，但配置已保存')
        success(t('apn.configSaved') || '配置已保存')
      }
      // 刷新UI状态
      currentTemplate.value = null
      await fetchConfig()
    } 
    // 手动模式：应用模板（仅当选择了模板时）
    else if (selectedTemplateId.value > 0) {
      const applyRes = await fetch('/api/apn/apply', {
        method: 'POST',
        headers: getHeaders(true),
        body: JSON.stringify({ template_id: selectedTemplateId.value })
      })
      if (applyRes.ok) {
        success(t('apn.templateApplied'))
        // 刷新UI状态
        await fetchConfig()
      } else {
        // 尝试获取错误详情
        let errMsg = '应用模板失败'
        try {
          const errData = await applyRes.json()
          if (errData.error) errMsg = errData.error
        } catch {}
        console.warn(errMsg + '，但配置已保存')
        success(t('apn.configSaved') || '配置已保存（模板将在重启后生效）')
      }
    }
  } catch (e) {
    error(t('apn.saveFailed') + ': ' + e.message)
  } finally {
    saving.value = false
  }
}

// 打开新建对话框
function openNewDialog() {
  isEditing.value = false
  form.value = { id: 0, name: '', apn: '', protocol: 'dual', username: '', password: '', auth_method: 'chap' }
  showDialog.value = true
}

// 打开编辑对话框
function openEditDialog(tpl) {
  isEditing.value = true
  form.value = { ...tpl }
  showDialog.value = true
}

// 应用预设
function applyPreset(preset) {
  form.value.name = preset.name
  form.value.apn = preset.apn
}

// 保存模板
async function saveTemplate() {
  if (!form.value.name.trim() || !form.value.apn.trim()) {
    error(t('apn.nameAndApnRequired'))
    return
  }
  
  saving.value = true
  try {
    const url = isEditing.value ? `/api/apn/templates/${form.value.id}` : '/api/apn/templates'
    const method = isEditing.value ? 'PUT' : 'POST'
    const res = await fetch(url, { method, headers: getHeaders(true), body: JSON.stringify(form.value) })
    if (!res.ok) throw new Error('保存失败')
    success(isEditing.value ? t('apn.templateUpdated') : t('apn.templateCreated'))
    showDialog.value = false
    await fetchTemplates()
  } catch (e) {
    error(e.message)
  } finally {
    saving.value = false
  }
}

// 删除模板
async function deleteTemplate(tpl) {
  // 检测是否为正在使用中的模板（手动模式且选中了该模板）
  if (currentMode.value === 1 && selectedTemplateId.value === tpl.id) {
    const switchOk = await confirm({ 
      title: t('apn.deleteTemplate'), 
      message: t('apn.activeTemplateHint') || '该模板正在使用中，需要先切换到自动模式才能删除。是否自动切换？', 
      danger: true 
    })
    if (!switchOk) return
    
    // 自动切换到自动模式并删除模板
    try {
      // 1. 先切换到自动模式
      const configRes = await fetch('/api/apn/config', {
        method: 'POST',
        headers: getHeaders(true),
        body: JSON.stringify({ mode: 0, template_id: 0, auto_start: 0 })
      })
      if (!configRes.ok) throw new Error('切换自动模式失败')
      
      // 2. 删除模板
      const deleteRes = await fetch(`/api/apn/templates/${tpl.id}`, { method: 'DELETE', headers: getHeaders() })
      if (!deleteRes.ok) throw new Error('删除模板失败')
      
      // 3. 更新本地状态
      const index = templates.value.findIndex(t => t.id === tpl.id)
      if (index > -1) {
        templates.value.splice(index, 1)
      }
      selectedTemplateId.value = 0
      currentTemplate.value = null
      await nextTick()
      currentMode.value = 0
      
      success(t('apn.switchedToAuto') || '已切换到自动模式并删除模板')
    } catch (e) {
      error(e.message)
    }
    return
  }
  
  // 普通删除流程
  const ok = await confirm({ title: t('apn.deleteTemplate'), message: `确定删除 "${tpl.name}"?`, danger: true })
  if (!ok) return
  
  try {
    const res = await fetch(`/api/apn/templates/${tpl.id}`, { method: 'DELETE', headers: getHeaders() })
    if (!res.ok) throw new Error('删除失败')
    
    // 先从本地数组移除，避免DOM更新问题
    const index = templates.value.findIndex(t => t.id === tpl.id)
    if (index > -1) {
      templates.value.splice(index, 1)
    }
    if (selectedTemplateId.value === tpl.id) {
      selectedTemplateId.value = 0
    }
    
    success(t('apn.templateDeleted'))
  } catch (e) {
    error(e.message)
    // 出错时重新获取列表
    await fetchTemplates()
  }
}

// 初始化
onMounted(async () => {
  loading.value = true
  await Promise.all([fetchConfig(), fetchTemplates()])
  loading.value = false
})
</script>

<template>
  <div class="space-y-6">
    <!-- 模式选择 -->
    <div class="bg-white/95 dark:bg-white/5 rounded-2xl border border-slate-200/60 dark:border-white/10 p-6 shadow-lg">
      <h3 class="text-slate-900 dark:text-white font-semibold mb-4 flex items-center">
        <i class="fas fa-sliders-h text-teal-500 mr-2"></i>
        {{ t('apn.modeSelection') }}
      </h3>
      
      <div class="grid grid-cols-2 gap-4">
        <!-- 自动模式 -->
        <button
          @click="currentMode = 0"
          :class="[
            'p-5 rounded-xl border-2 text-left transition-all',
            currentMode === 0
              ? 'bg-blue-500/10 dark:bg-blue-500/20 border-blue-500 shadow-lg'
              : 'bg-slate-50 dark:bg-white/5 border-slate-200 dark:border-white/10 hover:border-slate-400'
          ]"
        >
          <div class="flex items-center justify-between mb-2">
            <div class="flex items-center space-x-3">
              <div :class="['w-10 h-10 rounded-xl flex items-center justify-center', currentMode === 0 ? 'bg-blue-500' : 'bg-slate-200 dark:bg-white/10']">
                <i :class="['fas fa-magic', currentMode === 0 ? 'text-white' : 'text-slate-400']"></i>
              </div>
              <span class="text-slate-900 dark:text-white font-semibold">{{ t('apn.autoMode') }}</span>
            </div>
            <!-- 右侧选择指示器 -->
            <div v-if="currentMode === 0" class="w-6 h-6 rounded-full bg-blue-500 flex items-center justify-center">
              <i class="fas fa-check text-white text-xs"></i>
            </div>
            <div v-else class="w-6 h-6 rounded-full border-2 border-slate-300 dark:border-white/30"></div>
          </div>
          <p class="text-slate-500 dark:text-white/50 text-sm pl-13">{{ t('apn.autoModeDesc') }}</p>
        </button>
        
        <!-- 手动模式 -->
        <button
          @click="currentMode = 1"
          :class="[
            'p-5 rounded-xl border-2 text-left transition-all',
            currentMode === 1
              ? 'bg-teal-500/10 dark:bg-teal-500/20 border-teal-500 shadow-lg'
              : 'bg-slate-50 dark:bg-white/5 border-slate-200 dark:border-white/10 hover:border-slate-400'
          ]"
        >
          <div class="flex items-center justify-between mb-2">
            <div class="flex items-center space-x-3">
              <div :class="['w-10 h-10 rounded-xl flex items-center justify-center', currentMode === 1 ? 'bg-teal-500' : 'bg-slate-200 dark:bg-white/10']">
                <i :class="['fas fa-cog', currentMode === 1 ? 'text-white' : 'text-slate-400']"></i>
              </div>
              <span class="text-slate-900 dark:text-white font-semibold">{{ t('apn.manualMode') }}</span>
            </div>
            <!-- 右侧选择指示器 -->
            <div v-if="currentMode === 1" class="w-6 h-6 rounded-full bg-teal-500 flex items-center justify-center">
              <i class="fas fa-check text-white text-xs"></i>
            </div>
            <div v-else class="w-6 h-6 rounded-full border-2 border-slate-300 dark:border-white/30"></div>
          </div>
          <p class="text-slate-500 dark:text-white/50 text-sm pl-13">{{ t('apn.manualModeDesc') }}</p>
        </button>
      </div>
    </div>

    <!-- 自动模式提示 -->
    <div v-show="currentMode === 0" class="space-y-4">
      <!-- 主提示 -->
      <div class="bg-blue-50 dark:bg-blue-500/10 rounded-2xl border border-blue-200 dark:border-blue-500/20 p-6">
        <div class="flex items-start space-x-4">
          <div class="w-12 h-12 rounded-xl bg-blue-100 dark:bg-blue-500/20 flex items-center justify-center flex-shrink-0">
            <i class="fas fa-info-circle text-blue-500 text-xl"></i>
          </div>
          <div>
            <p class="text-blue-800 dark:text-blue-200 font-medium">{{ t('apn.autoModeHint') }}</p>
            <p class="text-blue-600 dark:text-blue-300 text-sm mt-1">{{ t('apn.clearHint') }}</p>
          </div>
        </div>
      </div>
      
      <!-- APN说明卡片 -->
      <div class="bg-slate-50 dark:bg-white/5 rounded-2xl border border-slate-200 dark:border-white/10 p-5">
        <div class="flex items-start space-x-3">
          <div class="w-10 h-10 rounded-lg bg-teal-100 dark:bg-teal-500/20 flex items-center justify-center flex-shrink-0">
            <i class="fas fa-sim-card text-teal-500"></i>
          </div>
          <div>
            <p class="text-slate-800 dark:text-white font-medium">{{ t('apn.apnSettingsTitle') || 'APN设置' }}</p>
            <p class="text-slate-500 dark:text-white/60 text-sm mt-1 leading-relaxed">
              {{ t('apn.apnDescription') }}
            </p>
          </div>
        </div>
      </div>
    </div>

    <!-- 手动模式：模板管理 -->
    <div v-show="currentMode === 1" class="grid grid-cols-1 lg:grid-cols-2 gap-6">
      <!-- 模板列表 -->
      <div class="bg-white/95 dark:bg-white/5 rounded-2xl border border-slate-200/60 dark:border-white/10 p-6 shadow-lg">
        <div class="flex items-center justify-between mb-4">
          <h3 class="text-slate-900 dark:text-white font-semibold flex items-center">
            <i class="fas fa-layer-group text-teal-500 mr-2"></i>
            {{ t('apn.templateList') }}
          </h3>
          <button @click="openNewDialog" class="px-3 py-1.5 bg-teal-500 text-white text-sm rounded-lg hover:bg-teal-600 transition-colors flex items-center">
            <i class="fas fa-plus mr-1.5"></i>
            {{ t('apn.newTemplate') }}
          </button>
        </div>
        
        <div class="space-y-2 max-h-80 overflow-y-auto">
          <template v-if="templates.length > 0">
            <div
              v-for="tpl in templates"
              :key="tpl.id"
              @click="selectedTemplateId = tpl.id"
              :class="[
                'p-4 rounded-xl border-2 cursor-pointer transition-all',
                selectedTemplateId === tpl.id
                  ? 'bg-teal-500/10 dark:bg-teal-500/20 border-teal-500'
                  : 'bg-slate-50 dark:bg-white/5 border-slate-200 dark:border-white/10 hover:border-slate-400'
              ]"
            >
              <div class="flex items-center justify-between">
                <div class="flex items-center space-x-3">
                  <div :class="['w-9 h-9 rounded-lg flex items-center justify-center', selectedTemplateId === tpl.id ? 'bg-teal-500' : 'bg-slate-200 dark:bg-white/10']">
                    <i :class="['fas fa-sim-card', selectedTemplateId === tpl.id ? 'text-white' : 'text-slate-400']"></i>
                  </div>
                  <div>
                    <p class="text-slate-900 dark:text-white font-medium">{{ tpl.name }}</p>
                    <p class="text-slate-500 dark:text-white/50 text-xs font-mono">{{ tpl.apn }}</p>
                  </div>
                </div>
                <div class="flex items-center space-x-2">
                  <div :class="['w-5 h-5 rounded-full border-2 flex items-center justify-center transition-all', selectedTemplateId === tpl.id ? 'border-teal-500 bg-teal-500' : 'border-slate-300 dark:border-white/30']">
                    <i v-if="selectedTemplateId === tpl.id" class="fas fa-check text-white text-xs"></i>
                  </div>
                  <button @click.stop="openEditDialog(tpl)" class="w-8 h-8 rounded-lg bg-slate-200 dark:bg-white/10 hover:bg-blue-100 dark:hover:bg-blue-500/20 flex items-center justify-center transition-colors">
                    <i class="fas fa-pen text-blue-500 text-xs"></i>
                  </button>
                  <button @click.stop="deleteTemplate(tpl)" class="w-8 h-8 rounded-lg bg-slate-200 dark:bg-white/10 hover:bg-red-100 dark:hover:bg-red-500/20 flex items-center justify-center transition-colors">
                    <i class="fas fa-trash-alt text-red-500 text-xs"></i>
                  </button>
                </div>
              </div>
            </div>
          </template>
          <div v-else class="text-center py-12">
            <i class="fas fa-folder-open text-slate-300 dark:text-white/20 text-4xl mb-4 block"></i>
            <p class="text-slate-500 dark:text-white/50">{{ t('apn.noTemplates') }}</p>
          </div>
        </div>
      </div>

      <!-- 状态与设置 -->
      <div class="space-y-6">
        <!-- 当前状态 -->
        <div class="bg-white/95 dark:bg-white/5 rounded-2xl border border-slate-200/60 dark:border-white/10 p-6 shadow-lg">
          <h3 class="text-slate-900 dark:text-white font-semibold mb-4 flex items-center">
            <i class="fas fa-info-circle text-blue-500 mr-2"></i>
            {{ t('apn.currentStatus') }}
          </h3>
          <div class="space-y-3">
            <div class="flex justify-between p-3 bg-slate-50 dark:bg-white/5 rounded-xl">
              <span class="text-slate-600 dark:text-white/60">{{ t('apn.boundTemplate') }}</span>
              <span class="text-slate-900 dark:text-white font-medium">{{ selectedTemplate?.name || t('apn.notSelected') }}</span>
            </div>
            <div v-if="selectedTemplate" class="flex justify-between p-3 bg-slate-50 dark:bg-white/5 rounded-xl">
              <span class="text-slate-600 dark:text-white/60">APN</span>
              <span class="text-slate-900 dark:text-white font-mono">{{ selectedTemplate.apn }}</span>
            </div>
            <!-- 应用状态 -->
            <div v-if="currentTemplate" class="flex justify-between items-center p-3 bg-slate-50 dark:bg-white/5 rounded-xl">
              <span class="text-slate-600 dark:text-white/60">{{ t('apn.applyStatus') || '应用状态' }}</span>
              <span :class="[
                'px-2.5 py-1 rounded-lg text-xs font-medium',
                currentTemplate.is_applied 
                  ? 'bg-green-100 dark:bg-green-500/20 text-green-600 dark:text-green-400' 
                  : 'bg-amber-100 dark:bg-amber-500/20 text-amber-600 dark:text-amber-400'
              ]">
                <i :class="currentTemplate.is_applied ? 'fas fa-check-circle' : 'fas fa-exclamation-circle'" class="mr-1"></i>
                {{ currentTemplate.is_applied ? (t('apn.applied') || '已应用') : (t('apn.notApplied') || '未应用') }}
              </span>
            </div>
            <!-- 连接状态 -->
            <div v-if="currentTemplate && currentTemplate.is_applied" class="flex justify-between items-center p-3 bg-slate-50 dark:bg-white/5 rounded-xl">
              <span class="text-slate-600 dark:text-white/60">{{ t('apn.connectionStatus') || '连接状态' }}</span>
              <span :class="[
                'px-2.5 py-1 rounded-lg text-xs font-medium flex items-center',
                currentTemplate.is_active 
                  ? 'bg-green-100 dark:bg-green-500/20 text-green-600 dark:text-green-400' 
                  : 'bg-slate-100 dark:bg-white/10 text-slate-500 dark:text-white/50'
              ]">
                <span :class="['w-2 h-2 rounded-full mr-1.5', currentTemplate.is_active ? 'bg-green-500 animate-pulse' : 'bg-slate-400']"></span>
                {{ currentTemplate.is_active ? (t('apn.active') || '已激活') : (t('apn.inactive') || '未激活') }}
              </span>
            </div>
            <!-- Context路径 -->
            <div v-if="currentTemplate && currentTemplate.is_applied && currentTemplate.applied_context" class="flex justify-between p-3 bg-slate-50 dark:bg-white/5 rounded-xl">
              <span class="text-slate-600 dark:text-white/60">Context</span>
              <span class="text-slate-900 dark:text-white font-mono text-sm">{{ currentTemplate.applied_context }}</span>
            </div>
          </div>
        </div>

        <!-- 自启动设置 -->
        <div v-show="selectedTemplateId > 0" class="bg-white/95 dark:bg-white/5 rounded-2xl border border-slate-200/60 dark:border-white/10 p-6 shadow-lg">
          <h3 class="text-slate-900 dark:text-white font-semibold mb-4 flex items-center">
            <i class="fas fa-power-off text-green-500 mr-2"></i>
            {{ t('apn.autoStart') }}
          </h3>
          <div class="flex items-center justify-between p-4 bg-slate-50 dark:bg-white/5 rounded-xl">
            <div class="flex items-center space-x-3">
              <div class="w-10 h-10 rounded-lg bg-green-100 dark:bg-green-500/20 flex items-center justify-center">
                <i class="fas fa-bolt text-green-500"></i>
              </div>
              <div>
                <p class="text-slate-900 dark:text-white font-medium">{{ t('apn.autoStart') }}</p>
                <p class="text-slate-500 dark:text-white/50 text-xs">{{ t('apn.autoStartDesc') }}</p>
              </div>
            </div>
            <label class="relative cursor-pointer">
              <input type="checkbox" v-model="autoStart" :true-value="1" :false-value="0" class="sr-only peer">
              <div class="w-14 h-7 bg-slate-300 dark:bg-white/20 rounded-full peer peer-checked:bg-green-500 transition-all"></div>
              <div class="absolute top-0.5 left-0.5 w-6 h-6 bg-white rounded-full shadow transition-transform peer-checked:translate-x-7"></div>
            </label>
          </div>
        </div>
      </div>
    </div>

    <!-- 保存按钮 -->
    <button
      @click="saveConfig"
      :disabled="saving || (currentMode === 1 && !selectedTemplateId)"
      class="w-full py-4 bg-gradient-to-r from-teal-500 to-cyan-500 text-white font-semibold rounded-xl hover:shadow-lg hover:scale-[1.02] active:scale-[0.98] transition-all disabled:opacity-50 disabled:cursor-not-allowed flex items-center justify-center space-x-2"
    >
      <i :class="saving ? 'fas fa-spinner animate-spin' : 'fas fa-save'"></i>
      <span>{{ saving ? t('common.saving') : t('common.save') }}</span>
    </button>

    <!-- 模板编辑弹窗 -->
    <Teleport to="body">
      <div v-show="showDialog" class="fixed inset-0 z-50 flex items-center justify-center p-4">
        <div class="absolute inset-0 bg-black/50 backdrop-blur-sm" @click="showDialog = false"></div>
        <div class="relative w-full max-w-lg bg-white dark:bg-slate-800 rounded-2xl shadow-2xl overflow-hidden">
          <!-- 头部 -->
          <div class="px-6 py-4 bg-teal-500 text-white flex items-center justify-between">
            <h3 class="font-semibold">{{ isEditing ? t('apn.editTemplate') : t('apn.newTemplate') }}</h3>
            <button @click="showDialog = false" class="w-8 h-8 rounded-lg hover:bg-white/20 flex items-center justify-center">
              <i class="fas fa-times"></i>
            </button>
          </div>
          
          <!-- 预设 -->
          <div v-show="!isEditing" class="px-6 py-3 bg-slate-50 dark:bg-white/5 border-b border-slate-200 dark:border-white/10">
            <p class="text-slate-600 dark:text-white/60 text-xs mb-2">{{ t('apn.quickPresets') }}</p>
            <div class="flex gap-2">
              <button v-for="p in presets" :key="p.apn" @click="applyPreset(p)" :class="[p.color, 'px-3 py-1.5 rounded-lg text-white text-xs font-medium hover:opacity-80']">
                {{ p.name }}
              </button>
            </div>
          </div>

          <!-- 表单 -->
          <div class="p-6 space-y-4">
            <div>
              <label class="block text-slate-700 dark:text-white/80 text-sm font-medium mb-1.5">
                {{ t('apn.templateName') }} <span class="text-red-500">*</span>
              </label>
              <input v-model="form.name" type="text" :placeholder="t('apn.templateNamePlaceholder')"
                class="w-full px-4 py-3 rounded-xl border border-slate-200 dark:border-white/10 bg-slate-50 dark:bg-white/5 text-slate-900 dark:text-white focus:border-teal-500 outline-none transition-all">
            </div>
            
            <div>
              <label class="block text-slate-700 dark:text-white/80 text-sm font-medium mb-1.5">
                {{ t('apn.apnName') }} <span class="text-red-500">*</span>
              </label>
              <input v-model="form.apn" type="text" :placeholder="t('apn.apnPlaceholder')"
                class="w-full px-4 py-3 rounded-xl border border-slate-200 dark:border-white/10 bg-slate-50 dark:bg-white/5 text-slate-900 dark:text-white font-mono focus:border-teal-500 outline-none transition-all">
            </div>
            
            <div>
              <label class="block text-slate-700 dark:text-white/80 text-sm font-medium mb-1.5">{{ t('apn.protocol') }}</label>
              <select v-model="form.protocol" class="w-full px-4 py-3 rounded-xl border border-slate-200 dark:border-white/10 bg-slate-50 dark:bg-slate-700 text-slate-900 dark:text-white focus:border-teal-500 outline-none appearance-none cursor-pointer">
                <option value="dual" class="bg-white dark:bg-slate-700">{{ t('apn.dualStack') }}</option>
                <option value="ip" class="bg-white dark:bg-slate-700">{{ t('apn.ipv4') }}</option>
                <option value="ipv6" class="bg-white dark:bg-slate-700">{{ t('apn.ipv6') }}</option>
              </select>
            </div>
            
            <div class="grid grid-cols-2 gap-4">
              <div>
                <label class="block text-slate-700 dark:text-white/80 text-sm font-medium mb-1.5">{{ t('apn.username') }}</label>
                <input v-model="form.username" type="text" :placeholder="t('apn.optional')"
                  class="w-full px-4 py-3 rounded-xl border border-slate-200 dark:border-white/10 bg-slate-50 dark:bg-white/5 text-slate-900 dark:text-white focus:border-teal-500 outline-none transition-all">
              </div>
              <div>
                <label class="block text-slate-700 dark:text-white/80 text-sm font-medium mb-1.5">{{ t('apn.password') }}</label>
                <input v-model="form.password" type="password" :placeholder="t('apn.optional')"
                  class="w-full px-4 py-3 rounded-xl border border-slate-200 dark:border-white/10 bg-slate-50 dark:bg-white/5 text-slate-900 dark:text-white focus:border-teal-500 outline-none transition-all">
              </div>
            </div>
            
            <div>
              <label class="block text-slate-700 dark:text-white/80 text-sm font-medium mb-1.5">{{ t('apn.authType') }}</label>
              <select v-model="form.auth_method" class="w-full px-4 py-3 rounded-xl border border-slate-200 dark:border-white/10 bg-slate-50 dark:bg-slate-700 text-slate-900 dark:text-white focus:border-teal-500 outline-none appearance-none cursor-pointer">
                <option value="chap" class="bg-white dark:bg-slate-700">CHAP</option>
                <option value="pap" class="bg-white dark:bg-slate-700">PAP</option>
                <option value="none" class="bg-white dark:bg-slate-700">{{ t('apn.noAuth') }}</option>
              </select>
            </div>
          </div>
          
          <!-- 底部按钮 -->
          <div class="px-6 py-4 bg-slate-50 dark:bg-white/5 border-t border-slate-200 dark:border-white/10 flex justify-end space-x-3">
            <button @click="showDialog = false" class="px-5 py-2.5 rounded-xl border border-slate-200 dark:border-white/10 text-slate-600 dark:text-white/60 hover:bg-slate-100 dark:hover:bg-white/10">
              {{ t('common.cancel') }}
            </button>
            <button @click="saveTemplate" :disabled="saving" class="px-5 py-2.5 rounded-xl bg-teal-500 text-white hover:bg-teal-600 disabled:opacity-50 flex items-center space-x-2">
              <i :class="saving ? 'fas fa-spinner animate-spin' : 'fas fa-check'"></i>
              <span>{{ t('common.save') }}</span>
            </button>
          </div>
        </div>
      </div>
    </Teleport>

    <!-- 加载遮罩 -->
    <Teleport to="body">
      <div v-show="loading" class="fixed inset-0 z-40 bg-black/30 backdrop-blur-sm flex items-center justify-center">
        <div class="bg-white dark:bg-slate-800 rounded-2xl p-6 shadow-2xl flex items-center space-x-4">
          <i class="fas fa-spinner animate-spin text-teal-500 text-2xl"></i>
          <span class="text-slate-700 dark:text-white">{{ t('common.loading') }}</span>
        </div>
      </div>
    </Teleport>
  </div>
</template>
