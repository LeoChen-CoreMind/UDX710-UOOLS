<script setup>
import { ref, onMounted } from 'vue'
import { useI18n } from 'vue-i18n'
import { useApi } from '../composables/useApi'

const { t } = useI18n()
const api = useApi()
const emit = defineEmits(['go-update'])

// 弹窗状态
const showModal = ref(false)
const newVersion = ref('')
const changelog = ref('')

// 检查是否需要检测更新（每天一次）
function shouldCheckToday() {
  const lastCheck = localStorage.getItem('update_last_check')
  const today = new Date().toISOString().split('T')[0]
  return lastCheck !== today
}

// 记录今日已检测
function markCheckedToday() {
  const today = new Date().toISOString().split('T')[0]
  localStorage.setItem('update_last_check', today)
}

// 检测更新
async function checkUpdate() {
  if (!shouldCheckToday()) return
  
  try {
    const res = await api.get('/api/update/check')
    markCheckedToday()
    
    if (res.has_update) {
      newVersion.value = res.latest_version
      changelog.value = res.changelog || ''
      showModal.value = true
    }
  } catch (e) {
    console.error('Auto update check failed:', e)
  }
}

// 去更新
function handleGoUpdate() {
  showModal.value = false
  emit('go-update')
}

// 稍后
function handleLater() {
  showModal.value = false
}

onMounted(() => {
  // 延迟2秒检测，避免影响页面加载
  setTimeout(checkUpdate, 2000)
})
</script>

<template>
  <!-- 更新提示弹窗 -->
  <Transition name="modal">
    <div v-if="showModal" class="fixed inset-0 z-[200] flex items-center justify-center p-4">
      <!-- 遮罩 -->
      <div class="absolute inset-0 bg-black/50 backdrop-blur-sm" @click="handleLater"></div>
      
      <!-- 弹窗内容 -->
      <div class="relative bg-white dark:bg-slate-800 rounded-2xl shadow-2xl max-w-sm w-full overflow-hidden">
        <!-- 内容区 -->
        <div class="p-6">
          <!-- 图标 -->
          <div class="w-16 h-16 mx-auto mb-4 rounded-2xl bg-gradient-to-br from-emerald-500 to-teal-500 flex items-center justify-center shadow-lg shadow-emerald-500/30">
            <font-awesome-icon icon="cloud-download-alt" class="text-white text-2xl" />
          </div>
          
          <!-- 标题 -->
          <h3 class="text-xl font-bold text-slate-900 dark:text-white text-center mb-2">
            {{ t('updateNotification.title') }}
          </h3>
          
          <!-- 版本信息 -->
          <p class="text-slate-600 dark:text-white/70 text-center mb-4">
            {{ t('updateNotification.message', { version: newVersion }) }}
          </p>
          
          <!-- 更新日志预览 -->
          <div v-if="changelog" class="mb-4 p-3 bg-slate-100 dark:bg-white/10 rounded-xl max-h-24 overflow-y-auto">
            <p class="text-slate-500 dark:text-white/50 text-sm">{{ changelog }}</p>
          </div>
          
          <!-- 按钮 -->
          <div class="flex space-x-3">
            <button 
              @click="handleLater"
              class="flex-1 py-3 bg-slate-200 dark:bg-white/10 text-slate-700 dark:text-white/70 font-medium rounded-xl hover:bg-slate-300 dark:hover:bg-white/20 transition-colors"
            >
              {{ t('updateNotification.later') }}
            </button>
            <button 
              @click="handleGoUpdate"
              class="flex-1 py-3 bg-gradient-to-r from-emerald-500 to-teal-500 text-white font-medium rounded-xl hover:shadow-lg hover:shadow-emerald-500/30 transition-all"
            >
              {{ t('updateNotification.goUpdate') }}
            </button>
          </div>
        </div>
      </div>
    </div>
  </Transition>
</template>

<style scoped>
.modal-enter-active, .modal-leave-active {
  transition: all 0.3s ease;
}
.modal-enter-from, .modal-leave-to {
  opacity: 0;
}
.modal-enter-from > div:last-child, .modal-leave-to > div:last-child {
  transform: scale(0.9) translateY(20px);
}
</style>
