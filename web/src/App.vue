<script setup>
import { ref, provide, onMounted, onUnmounted, computed } from 'vue'
import { useI18n } from 'vue-i18n'
import SystemMonitor from './components/SystemMonitor.vue'
import NetworkManager from './components/NetworkManager.vue'
import AdvancedNetwork from './components/AdvancedNetwork.vue'
import SmsManager from './components/SmsManager.vue'
import TrafficStats from './components/TrafficStats.vue'
import BatteryManager from './components/BatteryManager.vue'
import SystemSettings from './components/SystemSettings.vue'
import SystemUpdate from './components/SystemUpdate.vue'
import ATDebug from './components/ATDebug.vue'
import WebTerminal from './components/WebTerminal.vue'
import UsbMode from './components/UsbMode.vue'
import ApnConfig from './components/ApnConfig.vue'
import PluginStore from './components/PluginStore.vue'
import NetworkInterface from './components/NetworkInterface.vue'
import RatholeManager from './components/RatholeManager.vue'
import IPv6Proxy from './components/IPv6Proxy.vue'
import GlobalToast from './components/GlobalToast.vue'
import GlobalConfirm from './components/GlobalConfirm.vue'
import UpdateNotification from './components/UpdateNotification.vue'
import SecuritySetup from './components/SecuritySetup.vue'
import ForgotPasswordModal from './components/ForgotPasswordModal.vue'
import { isLoggedIn, authGetStatus, clearAuthToken, authLogin, getSecurityStatus } from './composables/useApi'
import { useToast } from './composables/useToast'

// i18n
const { t, locale } = useI18n()
const { success, error: showError } = useToast()

// 登录状态
const isAuthenticated = ref(false)
const authChecking = ref(true)
const showLoginModal = ref(false)

// 密保状态
const securitySet = ref(false)
const securityChecking = ref(true)

// 登录表单
const loginPassword = ref('')
const rememberPassword = ref(false)
const loggingIn = ref(false)
const showPassword = ref(false)
const showForgotModal = ref(false)

// 检查登录状态
async function checkAuthStatus() {
  authChecking.value = true
  try {
    // 先检查本地是否有token
    if (!isLoggedIn()) {
      isAuthenticated.value = false
      showLoginModal.value = true
      // 检查是否有保存的密码
      const savedPassword = localStorage.getItem('saved_password')
      if (savedPassword) {
        loginPassword.value = savedPassword
        rememberPassword.value = true
      }
      return
    }
    // 验证token是否有效
    const status = await authGetStatus()
    isAuthenticated.value = status.logged_in === true
    if (!isAuthenticated.value) {
      clearAuthToken()
      showLoginModal.value = true
      // 检查是否有保存的密码
      const savedPassword = localStorage.getItem('saved_password')
      if (savedPassword) {
        loginPassword.value = savedPassword
        rememberPassword.value = true
      }
    } else {
      // 登录成功后检查密保状态
      await checkSecurityStatus()
    }
  } catch (err) {
    console.error('Auth check error:', err)
    isAuthenticated.value = false
    clearAuthToken()
    showLoginModal.value = true
  } finally {
    authChecking.value = false
  }
}

// 检查密保状态
async function checkSecurityStatus() {
  securityChecking.value = true
  try {
    const res = await getSecurityStatus()
    securitySet.value = res.status === 'ok' && res.data?.is_set === true
  } catch (e) {
    console.error('Security check error:', e)
    securitySet.value = false
  } finally {
    securityChecking.value = false
  }
}

// 密保设置完成处理
function handleSecuritySetup() {
  securitySet.value = true
  success(t('security.setupSuccess'))
  fetchSystemInfo()
  startRefreshInterval()
}

// 处理登录
async function handleLogin() {
  if (!loginPassword.value) {
    showError(t('auth.enterPassword'))
    return
  }
  
  loggingIn.value = true
  try {
    const result = await authLogin(loginPassword.value)
    if (result.ok && result.data.token) {
      isAuthenticated.value = true
      showLoginModal.value = false
      success(t('auth.loginSuccess'))
      
      // 处理记住密码
      if (rememberPassword.value) {
        localStorage.setItem('saved_password', loginPassword.value)
      } else {
        localStorage.removeItem('saved_password')
      }
      
      // 登录成功后开始获取系统信息
      fetchSystemInfo()
      startRefreshInterval()
    } else {
      showError(result.data?.error || t('auth.wrongPassword'))
    }
  } catch (err) {
    showError(t('auth.loginFailed') + ': ' + err.message)
  } finally {
    loggingIn.value = false
  }
}

// 登录成功处理（兼容旧接口）
async function handleLoginSuccess() {
  isAuthenticated.value = true
  showLoginModal.value = false
  // 登录成功后检查密保状态
  await checkSecurityStatus()
  if (securitySet.value) {
    fetchSystemInfo()
    startRefreshInterval()
  }
}

// 登出处理
function handleLogout() {
  clearAuthToken()
  localStorage.removeItem('saved_password')
  isAuthenticated.value = false
  showLoginModal.value = true
  loginPassword.value = ''
  rememberPassword.value = false
  stopRefreshInterval()
}

// 跳转到更新页面
function handleGoUpdate() {
  activeMenu.value = 'update'
}

// 监听401事件
function handleAuthRequired() {
  isAuthenticated.value = false
  showLoginModal.value = true
}

// 切换语言
function toggleLocale() {
  const newLocale = locale.value === 'zh-CN' ? 'en-US' : 'zh-CN'
  locale.value = newLocale
  localStorage.setItem('locale', newLocale)
}

// 当前激活的菜单
const activeMenu = ref('monitor')
const sidebarCollapsed = ref(false)

// 移动端适配
const isMobile = ref(false)
const sidebarOpen = ref(false)

// 主题模式：true=深色(深夜模式), false=浅色(白天模式)
const isDark = ref(true)

// 初始化主题
function initTheme() {
  const saved = localStorage.getItem('theme')
  if (saved) {
    isDark.value = saved === 'dark'
  } else {
    // 默认深色模式（深夜模式）
    isDark.value = true
  }
  applyTheme()
}

// 应用主题到DOM
function applyTheme() {
  if (isDark.value) {
    document.documentElement.classList.add('dark')
  } else {
    document.documentElement.classList.remove('dark')
  }
}

// 切换主题
function toggleTheme() {
  isDark.value = !isDark.value
  localStorage.setItem('theme', isDark.value ? 'dark' : 'light')
  applyTheme()
}

// 提供主题状态给子组件
provide('isDark', isDark)

function checkMobile() {
  isMobile.value = window.innerWidth < 768
  if (isMobile.value) {
    sidebarCollapsed.value = true
    sidebarOpen.value = false
  }
}

// 滚动穿透处理
function lockBodyScroll(lock) {
  if (lock) {
    document.body.classList.add('body-lock')
  } else {
    document.body.classList.remove('body-lock')
  }
}

function toggleSidebar() {
  if (isMobile.value) {
    sidebarOpen.value = !sidebarOpen.value
    lockBodyScroll(sidebarOpen.value)
  } else {
    sidebarCollapsed.value = !sidebarCollapsed.value
  }
}

function handleMenuClick(menuId) {
  activeMenu.value = menuId
  if (isMobile.value) {
    sidebarOpen.value = false
    lockBodyScroll(false)
  }
}

// 系统信息数据
const systemInfo = ref({})
const loading = ref(false)
const lastUpdate = ref('')

provide('systemInfo', systemInfo)
provide('loading', loading)

// 菜单配置 - 使用i18n key
const menuItems = [
  { id: 'monitor', labelKey: 'menu.monitor', icon: 'fa-tachometer-alt', color: 'from-blue-500 to-cyan-400' },
  { id: 'network', labelKey: 'menu.network', icon: 'fa-network-wired', color: 'from-purple-500 to-pink-400' },
  { id: 'netif', labelKey: 'menu.netif', icon: 'fa-ethernet', color: 'from-teal-500 to-cyan-400' },
  { id: 'apn', labelKey: 'menu.apn', icon: 'fa-globe', color: 'from-teal-500 to-cyan-400' },
  { id: 'advanced', labelKey: 'menu.advanced', icon: 'fa-tower-cell', color: 'from-cyan-500 to-blue-500' },
  { id: 'sms', labelKey: 'menu.sms', icon: 'fa-envelope', color: 'from-emerald-500 to-teal-400' },
  { id: 'traffic', labelKey: 'menu.traffic', icon: 'fa-chart-area', color: 'from-green-500 to-emerald-400' },
  { id: 'battery', labelKey: 'menu.battery', icon: 'fa-battery-half', color: 'from-yellow-500 to-amber-400' },
  { id: 'rathole', labelKey: 'menu.rathole', icon: 'fa-shield-alt', color: 'from-indigo-500 to-violet-400' },
  { id: 'ipv6proxy', labelKey: 'menu.ipv6proxy', icon: 'fa-network-wired', color: 'from-cyan-500 to-blue-400' },
  { id: 'update', labelKey: 'menu.update', icon: 'fa-cloud-download-alt', color: 'from-violet-500 to-purple-400' },
  { id: 'at', labelKey: 'menu.at', icon: 'fa-terminal', color: 'from-cyan-500 to-teal-400' },
  { id: 'terminal', labelKey: 'menu.terminal', icon: 'fa-desktop', color: 'from-slate-500 to-gray-400' },
  { id: 'usb', labelKey: 'menu.usb', icon: ['fab', 'usb'], color: 'from-purple-500 to-pink-400' },
  { id: 'plugins', labelKey: 'menu.plugins', icon: 'fa-puzzle-piece', color: 'from-violet-500 to-purple-400' },
  { id: 'settings', labelKey: 'menu.settings', icon: 'fa-sliders-h', color: 'from-orange-500 to-amber-400' }
]

// 获取系统信息
async function fetchSystemInfo() {
  if (!isAuthenticated.value) return
  loading.value = true
  try {
    const token = localStorage.getItem('auth_token')
    const headers = token ? { 'Authorization': `Bearer ${token}` } : {}
    const response = await fetch('/api/info', { headers })
    if (!response.ok) {
      if (response.status === 401) {
        // Token失效，触发重新登录
        isAuthenticated.value = false
        localStorage.removeItem('auth_token')
        return
      }
      throw new Error('获取失败')
    }
    systemInfo.value = await response.json()
    lastUpdate.value = new Date().toLocaleTimeString()
  } catch (error) {
    console.error('获取系统信息错误:', error)
  } finally {
    loading.value = false
  }
}

// 提供登出函数给子组件
provide('handleLogout', handleLogout)

let refreshInterval = null

function startRefreshInterval() {
  if (refreshInterval) return
  refreshInterval = setInterval(fetchSystemInfo, 30000)
}

function stopRefreshInterval() {
  if (refreshInterval) {
    clearInterval(refreshInterval)
    refreshInterval = null
  }
}

onMounted(async () => {
  // 移动端检测
  checkMobile()
  window.addEventListener('resize', checkMobile)
  // 初始化主题
  initTheme()
  // 监听401事件
  window.addEventListener('auth-required', handleAuthRequired)
  
  // 检查登录状态
  await checkAuthStatus()
  
  // 如果已登录且密保已设置，开始获取系统信息
  if (isAuthenticated.value && securitySet.value) {
    fetchSystemInfo()
    startRefreshInterval()
  }
})
onUnmounted(() => {
  stopRefreshInterval()
  window.removeEventListener('resize', checkMobile)
  window.removeEventListener('auth-required', handleAuthRequired)
})
</script>

<template>
  <!-- 加载状态 -->
  <div v-if="authChecking" class="min-h-screen bg-gradient-to-br from-slate-50 via-blue-50/40 to-indigo-50/30 dark:from-slate-900 dark:via-slate-800 dark:to-slate-900 flex items-center justify-center">
    <div class="text-center">
      <div class="w-16 h-16 mx-auto mb-4 rounded-2xl bg-gradient-to-br from-blue-500 to-cyan-400 flex items-center justify-center shadow-lg shadow-blue-500/30 animate-pulse">
        <font-awesome-icon icon="wifi" class="text-white text-2xl" />
      </div>
      <p class="text-slate-500 dark:text-white/50">{{ t('common.loading') }}</p>
    </div>
  </div>

  <!-- 主界面 -->
  <div v-else class="min-h-screen bg-gradient-to-br from-slate-50 via-blue-50/40 to-indigo-50/30 dark:from-slate-900 dark:via-slate-800 dark:to-slate-900 flex transition-all duration-500">
    
    <!-- 登录弹窗遮罩 -->
    <Transition name="modal-backdrop">
      <div 
        v-if="showLoginModal"
        class="fixed inset-0 z-[100] flex items-center justify-center p-4"
      >
        <!-- 毛玻璃背景 -->
        <div class="absolute inset-0 bg-slate-900/60 dark:bg-black/70 backdrop-blur-md"></div>
        
        <!-- 登录弹窗 -->
        <Transition name="modal-content">
          <div 
            v-if="showLoginModal"
            class="relative w-full max-w-md"
          >
            <!-- 弹窗卡片 -->
            <div class="bg-white/95 dark:bg-slate-800/95 backdrop-blur-xl rounded-3xl shadow-2xl shadow-black/20 border border-white/20 dark:border-white/10 overflow-hidden">
              <!-- 内容区 -->
              <div class="p-8">
                <!-- Logo -->
                <div class="text-center mb-8">
                  <div class="w-20 h-20 mx-auto mb-4 rounded-2xl bg-gradient-to-br from-blue-500 to-cyan-400 flex items-center justify-center shadow-xl shadow-blue-500/30">
                    <font-awesome-icon icon="wifi" class="text-white text-3xl" />
                  </div>
                  <h1 class="text-2xl font-bold text-slate-900 dark:text-white">{{ t('header.title') }}</h1>
                  <p class="text-slate-500 dark:text-white/50 mt-1">{{ t('auth.welcomeBack') }}</p>
                </div>
                
                <!-- 登录表单 -->
                <form @submit.prevent="handleLogin" class="space-y-6">
                  <!-- 密码输入 -->
                  <div>
                    <label class="block text-slate-600 dark:text-white/60 text-sm font-medium mb-2">
                      <font-awesome-icon icon="lock" class="mr-2" />
                      {{ t('auth.password') }}
                    </label>
                    <div class="relative">
                      <input 
                        :type="showPassword ? 'text' : 'password'" 
                        v-model="loginPassword"
                        :placeholder="t('auth.enterPassword')"
                        class="w-full px-4 py-4 pr-12 bg-slate-100 dark:bg-white/10 border-2 border-transparent rounded-xl text-slate-900 dark:text-white placeholder-slate-400 dark:placeholder-white/30 focus:border-blue-500 focus:bg-white dark:focus:bg-white/5 transition-all text-lg"
                        autofocus
                      >
                      <button 
                        type="button"
                        @click.stop.prevent="showPassword = !showPassword"
                        class="absolute right-3 top-1/2 -translate-y-1/2 w-10 h-10 flex items-center justify-center text-slate-400 dark:text-white/30 hover:text-slate-600 dark:hover:text-white/60 z-10 select-none"
                        tabindex="-1"
                      >
                        <span class="relative w-5 h-5 flex items-center justify-center">
                          <font-awesome-icon icon="eye" class="absolute transition-all duration-200" :class="showPassword ? 'opacity-0 scale-75' : 'opacity-100 scale-100'" />
                          <font-awesome-icon icon="eye-slash" class="absolute transition-all duration-200" :class="showPassword ? 'opacity-100 scale-100' : 'opacity-0 scale-75'" />
                        </span>
                      </button>
                    </div>
                  </div>
                  
                  <!-- 记住密码 -->
                  <label class="flex items-center cursor-pointer group">
                    <div class="relative">
                      <input 
                        type="checkbox" 
                        v-model="rememberPassword"
                        class="sr-only peer"
                      >
                      <div class="w-5 h-5 bg-slate-200 dark:bg-white/10 rounded-md border-2 border-slate-300 dark:border-white/20 peer-checked:bg-blue-500 peer-checked:border-blue-500 transition-all flex items-center justify-center">
                        <font-awesome-icon 
                          v-if="rememberPassword" 
                          icon="check" 
                          class="text-white text-xs"
                        />
                      </div>
                    </div>
                    <span class="ml-3 text-slate-600 dark:text-white/60 text-sm group-hover:text-slate-900 dark:group-hover:text-white/80 transition-colors">
                      {{ t('auth.rememberPassword') || '记住密码' }}
                    </span>
                  </label>
                  
                  <!-- 登录按钮 -->
                  <button 
                    type="submit"
                    :disabled="loggingIn || !loginPassword"
                    class="w-full py-4 bg-gradient-to-r from-blue-500 to-cyan-400 text-white font-bold text-lg rounded-xl hover:shadow-lg hover:shadow-blue-500/30 hover:scale-[1.02] active:scale-[0.98] transition-all disabled:opacity-50 disabled:cursor-not-allowed disabled:hover:scale-100 disabled:hover:shadow-none"
                  >
                    <font-awesome-icon 
                      :icon="loggingIn ? 'spinner' : 'sign-in-alt'" 
                      :class="loggingIn ? 'animate-spin' : ''"
                      class="mr-2"
                    />
                    {{ loggingIn ? t('auth.loggingIn') : t('auth.login') }}
                  </button>
                </form>
                
                <!-- 忘记密码链接 -->
                <div class="mt-4 text-center">
                  <button 
                    type="button" 
                    @click="showLoginModal = false; showForgotModal = true"
                    class="text-blue-500 hover:text-blue-400 text-sm inline-flex items-center gap-2 transition-colors"
                  >
                    <font-awesome-icon icon="question-circle" />
                    {{ t('auth.forgotPassword') }}
                  </button>
                </div>
              </div>
              
              <!-- 底部提示 -->
              <div class="px-8 py-4 bg-slate-50 dark:bg-white/5 border-t border-slate-200 dark:border-white/10">
                <p class="text-center text-slate-500 dark:text-white/40 text-sm">
                  <font-awesome-icon icon="shield-alt" class="mr-1" />
                  {{ t('auth.loginToContinue') }}
                </p>
              </div>
            </div>
          </div>
        </Transition>
      </div>
    </Transition>
    
    <!-- 忘记密码弹窗 -->
    <ForgotPasswordModal 
      v-if="showForgotModal" 
      @close="showForgotModal = false; showLoginModal = true"
      @reset-success="showForgotModal = false; showLoginModal = true"
    />
    
    <!-- 移动端遮罩层 -->
    <div 
      v-if="isMobile && sidebarOpen"
      class="fixed inset-0 bg-black/40 dark:bg-black/60 z-40 backdrop-blur-sm"
      @click="sidebarOpen = false; lockBodyScroll(false)"
    ></div>

    <!-- 左侧侧边栏 -->
    <aside 
      class="fixed left-0 top-0 h-full z-50 transition-all duration-300 ease-in-out"
      :class="[
        isMobile 
          ? (sidebarOpen ? 'w-64 translate-x-0' : 'w-64 -translate-x-full') 
          : (sidebarCollapsed ? 'w-20' : 'w-64')
      ]"
    >
      <!-- 毛玻璃背景 -->
      <div class="absolute inset-0 bg-gradient-to-b from-white via-slate-50/95 to-blue-50/90 dark:from-white/10 dark:via-white/5 dark:to-white/10 backdrop-blur-xl border-r border-slate-200/70 dark:border-white/10 shadow-xl shadow-slate-200/20 dark:shadow-black/20 transition-all duration-500"></div>
      
      <div class="relative h-full flex flex-col">
        <!-- Logo区域 -->
        <div class="p-4 md:p-6 border-b border-slate-200 dark:border-white/10">
          <div class="flex items-center space-x-3">
            <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-blue-500 to-cyan-400 flex items-center justify-center shadow-lg shadow-blue-500/30">
              <font-awesome-icon icon="wifi" class="text-white text-lg" />
            </div>
            <div v-show="!sidebarCollapsed || isMobile" class="overflow-hidden">
              <h1 class="text-slate-900 dark:text-white font-bold text-lg whitespace-nowrap">{{ t('header.title') }}</h1>
              <p class="text-slate-500 dark:text-white/50 text-xs">{{ t('header.subtitle') }}</p>
            </div>
          </div>
        </div>

        <!-- 导航菜单 -->
        <nav class="flex-1 p-2 md:p-4 space-y-1 md:space-y-2 overflow-y-auto">
          <button
            v-for="item in menuItems"
            :key="item.id"
            @click="handleMenuClick(item.id)"
            class="w-full group relative overflow-hidden rounded-xl transition-all duration-300"
            :class="[
              activeMenu === item.id && (!sidebarCollapsed || isMobile) ? 'bg-gradient-to-r from-blue-100/80 to-indigo-100/60 dark:from-white/20 dark:to-white/10 shadow-md shadow-blue-200/30 dark:shadow-black/10' : '',
              activeMenu !== item.id ? 'hover:bg-slate-100/80 dark:hover:bg-white/10' : ''
            ]"
          >
            <div class="flex items-center p-2 md:p-3" :class="(sidebarCollapsed && !isMobile) ? 'justify-center' : 'space-x-3'">
              <div 
                class="w-9 h-9 md:w-10 md:h-10 rounded-lg flex items-center justify-center transition-all duration-300 flex-shrink-0"
                :class="activeMenu === item.id ? `bg-gradient-to-br ${item.color} shadow-lg` : 'bg-slate-200 dark:bg-white/5 group-hover:bg-slate-300 dark:group-hover:bg-white/10'"
              >
                <font-awesome-icon :icon="Array.isArray(item.icon) ? item.icon : item.icon.replace('fa-', '')" :class="activeMenu === item.id ? 'text-white' : 'text-slate-600 dark:text-white/60'" />
              </div>
              <span 
                v-show="!sidebarCollapsed || isMobile"
                class="font-medium transition-colors text-sm md:text-base"
                :class="activeMenu === item.id ? 'text-slate-900 dark:text-white' : 'text-slate-600 dark:text-white/60 group-hover:text-slate-900 dark:group-hover:text-white/80'"
              >
                {{ t(item.labelKey) }}
              </span>
            </div>
          </button>
        </nav>

        <!-- GitHub链接 -->
        <div class="px-2 md:px-4 pb-2">
          <a 
            href="https://github.com/LeoChen-CoreMind/UDX710-TOOLS" 
            target="_blank"
            class="w-full group relative overflow-hidden rounded-xl transition-all duration-300 hover:bg-slate-100/80 dark:hover:bg-white/10 flex items-center p-2 md:p-3"
            :class="(sidebarCollapsed && !isMobile) ? 'justify-center' : 'space-x-3'"
          >
            <div class="w-9 h-9 md:w-10 md:h-10 rounded-lg flex items-center justify-center transition-all duration-300 flex-shrink-0 bg-slate-200 dark:bg-white/5 group-hover:bg-gradient-to-br group-hover:from-gray-700 group-hover:to-gray-900">
              <font-awesome-icon :icon="['fab', 'github']" class="text-slate-600 dark:text-white/60 group-hover:text-white" />
            </div>
            <span 
              v-show="!sidebarCollapsed || isMobile"
              class="font-medium transition-colors text-sm md:text-base text-slate-600 dark:text-white/60 group-hover:text-slate-900 dark:group-hover:text-white/80"
            >
              {{ t('header.openSource') }}
            </span>
          </a>
        </div>

        <!-- 底部折叠按钮 (仅桌面端显示) -->
        <div v-if="!isMobile" class="p-4 border-t border-slate-200 dark:border-white/10">
          <button 
            @click="toggleSidebar"
            class="w-full p-3 rounded-xl bg-slate-200 dark:bg-white/5 hover:bg-slate-300 dark:hover:bg-white/10 transition-colors flex items-center justify-center"
          >
            <font-awesome-icon :icon="sidebarCollapsed ? 'chevron-right' : 'chevron-left'" class="text-slate-600 dark:text-white/60" />
          </button>
        </div>
      </div>
    </aside>

    <!-- 主内容区 -->
    <main 
      class="flex-1 transition-all duration-300 w-full"
      :class="isMobile ? 'ml-0' : (sidebarCollapsed ? 'ml-20' : 'ml-64')"
    >
      <!-- 顶部状态栏 -->
      <header class="sticky top-0 z-30 px-3 md:px-6 py-2 md:py-4">
        <div class="bg-white/90 dark:bg-white/10 backdrop-blur-xl rounded-xl md:rounded-2xl border border-slate-200/70 dark:border-white/10 px-3 md:px-6 py-3 md:py-4 shadow-lg shadow-slate-200/30 dark:shadow-black/20 transition-all duration-500">
          <div class="flex items-center justify-between gap-2 md:gap-4">
            <!-- 左侧：汉堡菜单 + 标题 -->
            <div class="flex items-center space-x-3">
              <!-- 汉堡菜单按钮 (仅移动端) -->
              <button 
                v-if="isMobile"
                @click="toggleSidebar"
                class="w-10 h-10 rounded-xl bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 transition-colors flex items-center justify-center flex-shrink-0"
              >
                <font-awesome-icon icon="bars" class="text-slate-700 dark:text-white/80" />
              </button>
              <div class="min-w-0">
                <h2 class="text-lg md:text-2xl font-bold text-slate-900 dark:text-white truncate">
                  {{ t(menuItems.find(m => m.id === activeMenu)?.labelKey) }}
                </h2>
                <p class="text-slate-500 dark:text-white/50 text-xs md:text-sm mt-0.5 md:mt-1 hidden sm:block">{{ t('header.realtimeMonitor') }}</p>
              </div>
            </div>
            <!-- 右侧：状态信息 -->
            <div class="flex items-center space-x-2 md:space-x-4 flex-shrink-0">
              <!-- 状态指示器 -->
              <div class="flex items-center space-x-1.5 md:space-x-2 px-2 md:px-4 py-1.5 md:py-2 bg-slate-200 dark:bg-white/5 rounded-lg md:rounded-xl">
                <span class="w-2 h-2 rounded-full animate-pulse" :class="loading ? 'bg-yellow-400' : 'bg-green-400'"></span>
                <span class="text-slate-600 dark:text-white/60 text-xs md:text-sm hidden sm:inline">{{ loading ? t('common.syncing') : t('common.connected') }}</span>
              </div>
              <!-- 语言切换按钮 -->
              <button 
                @click="toggleLocale"
                class="w-9 h-9 md:w-10 md:h-10 rounded-lg md:rounded-xl bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 transition-colors flex items-center justify-center"
                :title="locale === 'zh-CN' ? 'Switch to English' : '切换到中文'"
              >
                <font-awesome-icon icon="globe" class="text-blue-500 dark:text-blue-400 text-sm md:text-base" />
              </button>
              <!-- 主题切换按钮 -->
              <button 
                @click="toggleTheme"
                class="w-9 h-9 md:w-10 md:h-10 rounded-lg md:rounded-xl bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 transition-colors flex items-center justify-center"
                :title="isDark ? t('settings.lightMode') : t('settings.darkMode')"
              >
                <font-awesome-icon :icon="isDark ? 'sun' : 'moon'" class="text-amber-500 dark:text-amber-400 text-sm md:text-base" />
              </button>
              <!-- 刷新按钮 -->
              <button 
                @click="fetchSystemInfo"
                :disabled="loading"
                class="w-9 h-9 md:w-10 md:h-10 rounded-lg md:rounded-xl bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 transition-colors flex items-center justify-center"
              >
                <font-awesome-icon icon="sync-alt" class="text-slate-600 dark:text-white/60 text-sm md:text-base" :class="{ 'animate-spin': loading }" />
              </button>
              <!-- 时间显示 (仅桌面端) -->
              <div class="text-right hidden md:block">
                <div class="text-slate-500 dark:text-white/40 text-xs">{{ t('common.lastUpdate') }}</div>
                <div class="text-slate-700 dark:text-white/80 text-sm font-medium">{{ lastUpdate || '--:--:--' }}</div>
              </div>
            </div>
          </div>
        </div>
      </header>

      <!-- 内容区域 -->
      <div class="p-3 md:p-6">
        <!-- 密保设置优先显示 -->
        <SecuritySetup 
          v-if="!securitySet && !securityChecking"
          @setup-complete="handleSecuritySetup"
        />
        <!-- 正常内容区 -->
        <Transition v-else name="fade" mode="out-in">
          <SystemMonitor v-if="activeMenu === 'monitor'" key="monitor" />
          <NetworkManager v-else-if="activeMenu === 'network'" key="network" />
          <AdvancedNetwork v-else-if="activeMenu === 'advanced'" key="advanced" />
          <SmsManager v-else-if="activeMenu === 'sms'" key="sms" />
          <TrafficStats v-else-if="activeMenu === 'traffic'" key="traffic" />
          <BatteryManager v-else-if="activeMenu === 'battery'" key="battery" />
          <RatholeManager v-else-if="activeMenu === 'rathole'" key="rathole" />
          <IPv6Proxy v-else-if="activeMenu === 'ipv6proxy'" key="ipv6proxy" />
          <SystemUpdate v-else-if="activeMenu === 'update'" key="update" />
          <ATDebug v-else-if="activeMenu === 'at'" key="at" />
          <WebTerminal v-else-if="activeMenu === 'terminal'" key="terminal" />
          <UsbMode v-else-if="activeMenu === 'usb'" key="usb" />
          <ApnConfig v-else-if="activeMenu === 'apn'" key="apn" />
          <NetworkInterface v-else-if="activeMenu === 'netif'" key="netif" />
          <PluginStore v-else-if="activeMenu === 'plugins'" key="plugins" />
          <SystemSettings v-else-if="activeMenu === 'settings'" key="settings" />
        </Transition>
      </div>
    </main>
    <GlobalToast />
    <GlobalConfirm />
    <!-- 更新通知弹窗 -->
    <UpdateNotification v-if="isAuthenticated" @go-update="handleGoUpdate" />
  </div>
</template>

<style>
.fade-enter-active {
  transition: all 0.4s cubic-bezier(0.4, 0, 0.2, 1);
}
.fade-leave-active {
  transition: all 0.25s cubic-bezier(0.4, 0, 0.2, 1);
}
.fade-enter-from {
  opacity: 0;
  transform: translateX(16px) scale(0.98);
}
.fade-leave-to {
  opacity: 0;
  transform: translateX(-16px) scale(0.98);
}

/* 登录弹窗动画 */
.modal-backdrop-enter-active {
  transition: all 0.3s ease-out;
}
.modal-backdrop-leave-active {
  transition: all 0.2s ease-in;
}
.modal-backdrop-enter-from,
.modal-backdrop-leave-to {
  opacity: 0;
}

.modal-content-enter-active {
  transition: all 0.4s cubic-bezier(0.34, 1.56, 0.64, 1);
}
.modal-content-leave-active {
  transition: all 0.2s ease-in;
}
.modal-content-enter-from {
  opacity: 0;
  transform: scale(0.9) translateY(20px);
}
.modal-content-leave-to {
  opacity: 0;
  transform: scale(0.95) translateY(-10px);
}

/* 隐藏浏览器原生密码显示按钮 */
input[type="password"]::-ms-reveal,
input[type="password"]::-ms-clear {
  display: none;
}
input[type="password"]::-webkit-credentials-auto-fill-button,
input[type="password"]::-webkit-clear-button {
  display: none;
}
/* Edge/Chrome 密码眼睛按钮 */
input::-ms-reveal {
  display: none;
}
</style>
