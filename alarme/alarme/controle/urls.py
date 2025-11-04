from django.urls import path
from . import views

urlpatterns = [
	path('', views.home, name='home'),    
	path('historico/', views.historico, name='historico'),    
	path('receber-dados/', views.receber_dados, name='receber_dados'), # para a esp  
	path('obter-estado-alarme/', views.obter_estado_alarme, name='obter_estado_alarme'), # para a esp    
	path('verificar-buzzer/', views.verificar_buzzer, name='verificar_buzzer'), #para a esp    
	path('resetar-buzzer/', views.resetar_buzzer, name='resetar_buzzer'), #para a esp,
]
