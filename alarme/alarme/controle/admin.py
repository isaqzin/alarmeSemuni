from django.contrib import admin
from .models import SensorData, ComandoParaESP

@admin.register(SensorData)
class SensorDataAdmin(admin.ModelAdmin):
    list_display = ('estado_alarme', 'criado_em')
    list_filter = ('estado_alarme',)
    ordering = ('-criado_em',)


@admin.register(ComandoParaESP)
class ComandoParaESPAdmin(admin.ModelAdmin):
    list_display = ('modo_desejado', 'disparar_buzzer', 'atualizado_em')
    list_filter = ('modo_desejado', 'disparar_buzzer')
    ordering = ('-atualizado_em',)