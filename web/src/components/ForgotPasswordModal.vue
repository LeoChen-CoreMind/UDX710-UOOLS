<script setup>
import { ref, onMounted } from 'vue'
import { useI18n } from 'vue-i18n'
import { getSecurityQuestions, securityResetPassword } from '../composables/useApi'
import { useToast } from '../composables/useToast'

const { t } = useI18n()
const { success, error: showError } = useToast()

const emit = defineEmits(['close', 'reset-success'])

// 状态
const loading = ref(true)
const notSetup = ref(false)
const questions = ref({ question1: '', question2: '' })
const answer1 = ref('')
const answer2 = ref('')
const confirmation = ref('')
const submitting = ref(false)

const CONFIRMATION_TEXT = '已知晓风险'

// 获取密保问题
onMounted(async () => {
  try {
    const res = await getSecurityQuestions()
    if (res.status === 'ok') {
      questions.value = res.data
    } else {
      // 未设置密保，显示提示
      notSetup.value = true
    }
  } catch (e) {
    // 404 或其他错误表示未设置密保
    notSetup.value = true
  } finally {
    loading.value = false
  }
})

// 重置密码
async function handleReset() {
  if (!answer1.value || !answer2.value) {
    showError(t('security.pleaseAnswer'))
    return
  }
  if (confirmation.value !== CONFIRMATION_TEXT) {
    showError(t('security.confirmationRequired'))
    return
  }
  
  submitting.value = true
  try {
    const res = await securityResetPassword(answer1.value, answer2.value, CONFIRMATION_TEXT)
    if (res.status === 'ok') {
      success(t('security.resetPasswordSuccess'))
      emit('reset-success')
      emit('close')
    } else {
      showError(res.message || t('security.resetPasswordFailed'))
    }
  } catch (e) {
    showError(t('security.resetPasswordFailed'))
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <!-- 遮罩层 -->
  <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/50 backdrop-blur-sm" @click.self="$emit('close')">
    <div class="w-full max-w-md mx-4 rounded-2xl bg-white dark:bg-slate-800 shadow-2xl overflow-hidden">
      <!-- 标题栏 -->
      <div class="px-6 py-4 bg-gradient-to-r from-amber-500 to-orange-400 flex items-center justify-between">
        <h3 class="text-white font-semibold text-lg">
          <i class="fas fa-key mr-2"></i>
          {{ t('auth.forgotPassword') }}
        </h3>
        <button @click="$emit('close')" class="text-white/80 hover:text-white transition-colors">
          <i class="fas fa-times text-xl"></i>
        </button>
      </div>
      
      <!-- 内容区 -->
      <div class="p-6">
        <!-- 加载状态 -->
        <div v-if="loading" class="py-8 text-center">
          <i class="fas fa-spinner animate-spin text-3xl text-amber-500"></i>
          <p class="mt-4 text-slate-500 dark:text-white/50">{{ t('common.loading') }}</p>
        </div>
        
        <!-- 未设置密保提示 -->
        <div v-else-if="notSetup" class="py-6 text-center space-y-4">
          <div class="w-16 h-16 mx-auto rounded-full bg-amber-100 dark:bg-amber-900/30 flex items-center justify-center">
            <i class="fas fa-exclamation-triangle text-3xl text-amber-500"></i>
          </div>
          <h4 class="text-lg font-medium text-slate-700 dark:text-white">{{ t('security.notSetupTitle') }}</h4>
          <p class="text-slate-500 dark:text-white/60 text-sm">{{ t('security.notSetupDesc') }}</p>
          <button 
            @click="$emit('close')"
            class="px-6 py-2 bg-slate-200 dark:bg-white/10 text-slate-700 dark:text-white rounded-lg hover:bg-slate-300 dark:hover:bg-white/20 transition-colors"
          >
            {{ t('common.close') }}
          </button>
        </div>
        
        <!-- 验证密保并重置 -->
        <div v-else class="space-y-4">
          <!-- 提示信息 -->
          <div class="p-3 rounded-lg bg-amber-50 dark:bg-amber-900/20 text-amber-700 dark:text-amber-300 text-sm">
            <i class="fas fa-info-circle mr-2"></i>
            {{ t('security.resetToDefaultHint') }}
          </div>
          
          <!-- 问题1 -->
          <div>
            <label class="block text-slate-600 dark:text-white/60 text-sm mb-2">{{ questions.question1 }}</label>
            <input 
              type="text"
              v-model="answer1"
              :placeholder="t('security.enterAnswer')"
              class="w-full px-4 py-3 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white focus:border-amber-400 focus:ring-2 focus:ring-amber-400/20 transition-all"
            />
          </div>
          
          <!-- 问题2 -->
          <div>
            <label class="block text-slate-600 dark:text-white/60 text-sm mb-2">{{ questions.question2 }}</label>
            <input 
              type="text"
              v-model="answer2"
              :placeholder="t('security.enterAnswer')"
              class="w-full px-4 py-3 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white focus:border-amber-400 focus:ring-2 focus:ring-amber-400/20 transition-all"
            />
          </div>
          
          <!-- 确认输入 -->
          <div class="pt-4 border-t border-slate-200 dark:border-white/10">
            <label class="block text-slate-600 dark:text-white/60 text-sm mb-2">
              {{ t('security.typeToConfirm') }}
              <span class="text-amber-500 font-medium">{{ CONFIRMATION_TEXT }}</span>
            </label>
            <input 
              type="text"
              v-model="confirmation"
              :placeholder="CONFIRMATION_TEXT"
              class="w-full px-4 py-3 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white focus:border-amber-400 focus:ring-2 focus:ring-amber-400/20 transition-all"
            />
          </div>
          
          <!-- 重置按钮 -->
          <button 
            @click="handleReset"
            :disabled="!answer1 || !answer2 || confirmation !== CONFIRMATION_TEXT || submitting"
            class="w-full py-3 mt-4 bg-gradient-to-r from-amber-500 to-orange-400 text-white font-medium rounded-xl hover:shadow-lg hover:shadow-amber-500/30 transition-all disabled:opacity-50 disabled:cursor-not-allowed"
          >
            <i :class="submitting ? 'fas fa-spinner animate-spin' : 'fas fa-undo'" class="mr-2"></i>
            {{ submitting ? t('common.loading') : t('security.resetToDefault') }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>
