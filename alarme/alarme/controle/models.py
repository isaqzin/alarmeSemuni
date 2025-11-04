from django.db import models

class SensorData(models.Model):
    estado_alarme = models.CharField(max_length=20)  
    criado_em = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"Estado: {self.estado_alarme} | Em: {self.criado_em}"

class ComandoParaESP(models.Model):
    modo_desejado = models.CharField(max_length=20, choices=[
        ('ligado', 'Ligado'),
        ('desligado', 'Desligado'),
        ('manual', 'Manual')
    ])
    disparar_buzzer = models.BooleanField(default=False)
    atualizado_em = models.DateTimeField(auto_now=True)