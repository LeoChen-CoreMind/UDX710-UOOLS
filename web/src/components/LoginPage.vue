<template>
  <div class="login-container">
    <div class="login-card">
      <div class="login-header">
        <i class="fas fa-shield-alt login-icon"></i>
        <h2>{{ $t('auth.title') }}</h2>
      </div>
      
      <form @submit.prevent="handleLogin" class="login-form">
        <div class="form-group">
          <label>{{ $t('auth.password') }}</label>
          <div class="password-input">
            <input 
              :type="showPassword ? 'text' : 'password'"
              v-model="password"
              :placeholder="$t('auth.passwordPlaceholder')"
              :disabled="loading"
              autofocus
            />
            <button 
              type="button" 
              class="toggle-password"
              @click.stop.prevent="showPassword = !showPassword"
              tabindex="-1"
            >
              <span class="icon-wrapper">
                <i class="fas fa-eye" :class="showPassword ? 'icon-hidden' : 'icon-visible'"></i>
                <i class="fas fa-eye-slash" :class="showPassword ? 'icon-visible' : 'icon-hidden'"></i>
              </span>
            </button>
          </div>
        </div>
        
        <div v-if="error" class="error-message">
          <i class="fas fa-exclamation-circle"></i>
          {{ error }}
        </div>
        
        <button type="submit" class="login-btn" :disabled="loading || !password">
          <i v-if="loading" class="fas fa-spinner fa-spin"></i>
          <i v-else class="fas fa-sign-in-alt"></i>
          {{ loading ? $t('auth.loggingIn') : $t('auth.login') }}
        </button>
      </form>
      
      <!-- 忘记密码链接 -->
      <div class="forgot-password">
        <button type="button" @click="showForgotModal = true" class="forgot-link">
          <i class="fas fa-question-circle"></i>
          {{ $t('auth.forgotPassword') }}
        </button>
      </div>
      
      <div class="login-footer">
        <p>{{ $t('auth.defaultPasswordHint') }}</p>
      </div>
    </div>
    
    <!-- 忘记密码弹窗 -->
    <ForgotPasswordModal 
      v-if="showForgotModal" 
      @close="showForgotModal = false"
      @reset-success="handleResetSuccess"
    />
  </div>
</template>

<script setup>
import { ref } from 'vue'
import { useI18n } from 'vue-i18n'
import ForgotPasswordModal from './ForgotPasswordModal.vue'

const { t } = useI18n()
const emit = defineEmits(['login-success'])

const password = ref('')
const showPassword = ref(false)
const loading = ref(false)
const error = ref('')
const showForgotModal = ref(false)

function handleResetSuccess() {
  // 密码重置成功后的处理
  error.value = ''
}

async function handleLogin() {
  if (!password.value) return
  
  loading.value = true
  error.value = ''
  
  try {
    const response = await fetch('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ password: password.value })
    })
    
    const data = await response.json()
    
    if (response.ok && data.token) {
      // 保存token到localStorage
      localStorage.setItem('auth_token', data.token)
      emit('login-success')
    } else {
      error.value = data.message || t('auth.loginFailed')
    }
  } catch (err) {
    error.value = t('auth.networkError')
    console.error('Login error:', err)
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-container {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
  padding: 20px;
}

.login-card {
  background: #1e1e2e;
  border-radius: 16px;
  padding: 40px;
  width: 100%;
  max-width: 400px;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
}

.login-header {
  text-align: center;
  margin-bottom: 30px;
}

.login-icon {
  font-size: 48px;
  color: #4fc3f7;
  margin-bottom: 16px;
}

.login-header h2 {
  color: #fff;
  font-size: 24px;
  font-weight: 600;
  margin: 0;
}

.login-form {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.form-group label {
  display: block;
  color: #a0a0a0;
  font-size: 14px;
  margin-bottom: 8px;
}

.password-input {
  position: relative;
  display: flex;
  align-items: center;
}

.password-input input {
  width: 100%;
  padding: 14px 50px 14px 16px;
  background: #2a2a3e;
  border: 1px solid #3a3a4e;
  border-radius: 8px;
  color: #fff;
  font-size: 16px;
  transition: border-color 0.2s;
}

.password-input input:focus {
  outline: none;
  border-color: #4fc3f7;
}

.password-input input::placeholder {
  color: #666;
}

.toggle-password {
  position: absolute;
  right: 12px;
  background: none;
  border: none;
  color: #666;
  cursor: pointer;
  padding: 8px;
  transition: color 0.2s;
  user-select: none;
}

.toggle-password:hover {
  color: #4fc3f7;
}

.icon-wrapper {
  position: relative;
  display: flex;
  align-items: center;
  justify-content: center;
  width: 20px;
  height: 20px;
}

.icon-wrapper i {
  position: absolute;
  transition: opacity 0.2s ease, transform 0.2s ease;
}

.icon-visible {
  opacity: 1;
  transform: scale(1);
}

.icon-hidden {
  opacity: 0;
  transform: scale(0.75);
}

.error-message {
  background: rgba(244, 67, 54, 0.1);
  border: 1px solid rgba(244, 67, 54, 0.3);
  border-radius: 8px;
  padding: 12px 16px;
  color: #f44336;
  font-size: 14px;
  display: flex;
  align-items: center;
  gap: 8px;
}

.login-btn {
  width: 100%;
  padding: 14px;
  background: linear-gradient(135deg, #4fc3f7 0%, #29b6f6 100%);
  border: none;
  border-radius: 8px;
  color: #fff;
  font-size: 16px;
  font-weight: 600;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  transition: transform 0.2s, box-shadow 0.2s;
}

.login-btn:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 8px 20px rgba(79, 195, 247, 0.3);
}

.login-btn:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.forgot-password {
  margin-top: 16px;
  text-align: center;
}

.forgot-link {
  background: none;
  border: none;
  color: #4fc3f7;
  font-size: 14px;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  transition: color 0.2s;
}

.forgot-link:hover {
  color: #81d4fa;
}

.login-footer {
  margin-top: 24px;
  text-align: center;
}

.login-footer p {
  color: #666;
  font-size: 12px;
  margin: 0;
}
</style>
