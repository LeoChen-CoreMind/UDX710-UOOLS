<script setup>
import { ref, computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { setupSecurityQuestions } from '../composables/useApi'
import { useToast } from '../composables/useToast'

const { t } = useI18n()
const { success, error: showError } = useToast()

const emit = defineEmits(['setup-complete'])

// 预设问题列表
const presetQuestions = computed(() => [
  t('security.presetQ1'),
  t('security.presetQ2'),
  t('security.presetQ3'),
  t('security.presetQ4'),
  t('security.presetQ5'),
  t('security.presetQ6'),
  t('security.presetQ7'),
  t('security.presetQ8')
])

// 表单数据
const question1 = ref('')
const answer1 = ref('')
const question2 = ref('')
const answer2 = ref('')
const customQ1 = ref('')
const customQ2 = ref('')
const submitting = ref(false)

// 实际问题（支持自定义）
const getQ1 = () => question1.value === '__custom__' ? customQ1.value : question1.value
const getQ2 = () => question2.value === '__custom__' ? customQ2.value : question2.value

// 表单验证
const isValid = () => {
  const q1 = getQ1()
  const q2 = getQ2()
  return q1 && answer1.value && q2 && answer2.value && q1 !== q2
}

// 提交设置
async function handleSubmit() {
  if (!isValid()) {
    showError(t('security.invalidForm'))
    return
  }
  
  submitting.value = true
  try {
    const res = await setupSecurityQuestions(getQ1(), answer1.value, getQ2(), answer2.value)
    if (res.status === 'ok') {
      success(t('security.setupSuccess'))
      emit('setup-complete')
    } else {
      showError(res.message || t('security.setupFailed'))
    }
  } catch (e) {
    showError(t('security.setupFailed'))
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <div class="max-w-2xl mx-auto space-y-6">
    <!-- 标题卡片 -->
    <div class="rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 p-6 shadow-lg shadow-slate-200/40 dark:shadow-black/20">
      <div class="flex items-center space-x-4">
        <div class="w-14 h-14 rounded-2xl bg-gradient-to-br from-green-500 to-emerald-400 flex items-center justify-center shadow-lg shadow-green-500/30">
          <i class="fas fa-shield-alt text-white text-xl"></i>
        </div>
        <div>
          <h2 class="text-xl font-bold text-slate-900 dark:text-white">{{ t('security.setupTitle') }}</h2>
          <p class="text-slate-500 dark:text-white/50 text-sm mt-1">{{ t('security.setupDesc') }}</p>
        </div>
      </div>
    </div>

    <!-- 警告提示 -->
    <div class="rounded-2xl bg-gradient-to-r from-red-500/10 to-orange-500/10 dark:from-red-500/20 dark:to-orange-500/20 border border-red-200 dark:border-red-500/30 p-5 shadow-lg">
      <div class="flex items-start space-x-4">
        <div class="w-10 h-10 rounded-xl bg-red-500/20 flex items-center justify-center flex-shrink-0">
          <i class="fas fa-exclamation-circle text-red-500 dark:text-red-400"></i>
        </div>
        <div>
          <h3 class="font-semibold text-red-700 dark:text-red-400 mb-1">{{ t('security.cannotChange') }}</h3>
          <p class="text-red-600/80 dark:text-red-300/80 text-sm">{{ t('security.setupWarning') }}</p>
        </div>
      </div>
    </div>

    <!-- 设置表单 -->
    <div class="rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 p-6 shadow-lg shadow-slate-200/40 dark:shadow-black/20">
      <!-- 问题1 -->
      <div class="mb-6">
        <label class="block text-slate-700 dark:text-white/70 text-sm font-medium mb-2">
          <i class="fas fa-question-circle mr-2 text-blue-500"></i>
          {{ t('security.question1') }}
        </label>
        <div class="relative">
          <select 
            v-model="question1"
            class="w-full px-4 py-3 pr-10 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white focus:border-green-400 focus:ring-2 focus:ring-green-400/20 transition-all appearance-none cursor-pointer"
          >
            <option value="" disabled class="bg-white dark:bg-slate-800 text-slate-500">{{ t('security.selectQuestion') }}</option>
            <option v-for="(q, i) in presetQuestions" :key="i" :value="q" class="bg-white dark:bg-slate-800 text-slate-900 dark:text-white">{{ q }}</option>
            <option value="__custom__" class="bg-white dark:bg-slate-800 text-blue-600 dark:text-blue-400">📝 {{ t('security.customQuestion') }}</option>
          </select>
          <i class="fas fa-chevron-down absolute right-4 top-1/2 -translate-y-1/2 text-slate-400 pointer-events-none"></i>
        </div>
        <input 
          v-if="question1 === '__custom__'"
          v-model="customQ1"
          :placeholder="t('security.enterCustomQuestion')"
          class="w-full mt-3 px-4 py-3 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white placeholder-slate-400 dark:placeholder-white/40 focus:border-green-400 focus:ring-2 focus:ring-green-400/20 transition-all"
        />
      </div>
      
      <!-- 答案1 -->
      <div class="mb-8">
        <label class="block text-slate-700 dark:text-white/70 text-sm font-medium mb-2">
          <i class="fas fa-key mr-2 text-amber-500"></i>
          {{ t('security.answer1') }}
        </label>
        <input 
          type="text"
          v-model="answer1"
          :placeholder="t('security.enterAnswer')"
          class="w-full px-4 py-3 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white placeholder-slate-400 dark:placeholder-white/40 focus:border-green-400 focus:ring-2 focus:ring-green-400/20 transition-all"
        />
      </div>
      
      <!-- 分隔线 -->
      <div class="flex items-center mb-8">
        <div class="flex-1 border-t border-slate-200 dark:border-white/10"></div>
        <span class="px-4 text-slate-400 dark:text-white/30 text-sm">问题二</span>
        <div class="flex-1 border-t border-slate-200 dark:border-white/10"></div>
      </div>
      
      <!-- 问题2 -->
      <div class="mb-6">
        <label class="block text-slate-700 dark:text-white/70 text-sm font-medium mb-2">
          <i class="fas fa-question-circle mr-2 text-purple-500"></i>
          {{ t('security.question2') }}
        </label>
        <div class="relative">
          <select 
            v-model="question2"
            class="w-full px-4 py-3 pr-10 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white focus:border-green-400 focus:ring-2 focus:ring-green-400/20 transition-all appearance-none cursor-pointer"
          >
            <option value="" disabled class="bg-white dark:bg-slate-800 text-slate-500">{{ t('security.selectQuestion') }}</option>
            <option v-for="(q, i) in presetQuestions" :key="i" :value="q" class="bg-white dark:bg-slate-800 text-slate-900 dark:text-white">{{ q }}</option>
            <option value="__custom__" class="bg-white dark:bg-slate-800 text-blue-600 dark:text-blue-400">📝 {{ t('security.customQuestion') }}</option>
          </select>
          <i class="fas fa-chevron-down absolute right-4 top-1/2 -translate-y-1/2 text-slate-400 pointer-events-none"></i>
        </div>
        <input 
          v-if="question2 === '__custom__'"
          v-model="customQ2"
          :placeholder="t('security.enterCustomQuestion')"
          class="w-full mt-3 px-4 py-3 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white placeholder-slate-400 dark:placeholder-white/40 focus:border-green-400 focus:ring-2 focus:ring-green-400/20 transition-all"
        />
      </div>
      
      <!-- 答案2 -->
      <div class="mb-8">
        <label class="block text-slate-700 dark:text-white/70 text-sm font-medium mb-2">
          <i class="fas fa-key mr-2 text-pink-500"></i>
          {{ t('security.answer2') }}
        </label>
        <input 
          type="text"
          v-model="answer2"
          :placeholder="t('security.enterAnswer')"
          class="w-full px-4 py-3 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white placeholder-slate-400 dark:placeholder-white/40 focus:border-green-400 focus:ring-2 focus:ring-green-400/20 transition-all"
        />
      </div>
      
      <!-- 提交按钮 -->
      <button 
        @click="handleSubmit"
        :disabled="!isValid() || submitting"
        class="w-full py-4 bg-gradient-to-r from-green-500 to-emerald-400 hover:from-green-600 hover:to-emerald-500 text-white font-semibold rounded-xl shadow-lg shadow-green-500/30 hover:shadow-xl hover:shadow-green-500/40 transition-all duration-300 disabled:opacity-50 disabled:cursor-not-allowed disabled:hover:shadow-lg"
      >
        <i :class="submitting ? 'fas fa-spinner animate-spin' : 'fas fa-check-circle'" class="mr-2"></i>
        {{ submitting ? t('common.loading') : t('security.confirmSetup') }}
      </button>
    </div>
  </div>
</template>
