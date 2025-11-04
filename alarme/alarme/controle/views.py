from django.shortcuts import render, redirect
from django.http import HttpResponse
from django.http import JsonResponse
from django.views.decorators.csrf import csrf_exempt
import json
from .models import SensorData
from .models import ComandoParaESP, SensorData
from django.utils import timezone
import requests
import threading

# Configurações do bot do Telegram  
CHAT_ID = '-5026161595'



def enviar_mensagem(chat_id, texto):
    url = f'https://api.telegram.org/bot8189702580:AAFFsqSaaRYD6NM5Q1lrcIHNR7-CsLrz-Ps/sendMessage'
    data = {
        'chat_id': chat_id,
        'text': texto
    }

    response = requests.post(url, data=data)
    if response.status_code != 200:
        print(f"Erro ao enviar mensagem: {response.text}")

def enviar_em_thread(chat_id, texto):
    t = threading.Thread(target=enviar_mensagem, args=(chat_id, texto))
    t.start()


def historico(request):
    estado_filtro = request.GET.get('estado')

    if estado_filtro in ['ligado', 'desligado', 'automatico']:
        dados = SensorData.objects.filter(estado_alarme=estado_filtro).order_by('-criado_em')
    else:
        dados = SensorData.objects.all().order_by('-criado_em')

    return render(request, 'historico.html', {
        'dados': dados,
        'estado_filtro': estado_filtro
    })

@csrf_exempt
def home(request):
    # Tenta buscar o último comando
    ultimo_cmd = ComandoParaESP.objects.order_by('-id').last()

    # Define valores padrão caso não exista nenhum registro
    if ultimo_cmd:
        data = ultimo_cmd.atualizado_em
        buzzer = ultimo_cmd.disparar_buzzer
        estado = ultimo_cmd.modo_desejado
    else:
        data = None
        buzzer = False
        estado = "desligado"  
        ultimo_cmd = ComandoParaESP.objects.create(
            modo_desejado=estado,
            disparar_buzzer=buzzer
        )

    print(estado)

    if request.method == 'POST':
        acao = request.POST.get('acao')

        if acao in ['ligado', 'desligado', 'manual']:
            cmd = ComandoParaESP.objects.order_by('-id').last()
            if cmd:
                cmd.modo_desejado = acao
                cmd.save()
            else:
                ComandoParaESP.objects.create(modo_desejado=acao, disparar_buzzer=False)
            return redirect('home')

        elif acao == 'buzzer_on':
            cmd = ComandoParaESP.objects.order_by('-id').last()
            if cmd:
                cmd.disparar_buzzer = True
                cmd.save()
            else:
                ComandoParaESP.objects.create(modo_desejado=estado, disparar_buzzer=True)
            return redirect('home')

        elif acao == 'buzzer_off':
            cmd = ComandoParaESP.objects.order_by('-id').last()
            if cmd:
                cmd.disparar_buzzer = False
                cmd.save()
            return redirect('home')

        elif acao == 'historico':
            return redirect('historico')

    contexto = {
        'estado_atual': estado,
        'data': data,
        'ultimo_cmd': ultimo_cmd,
        'buzzer': buzzer
    }

    return render(request, 'home.html', contexto)


@csrf_exempt
def receber_dados(request):
    if request.method == 'POST':
        try:
            data = json.loads(request.body)
            estado = data.get('estado_alarme')

            if estado in ['ligado', 'desligado', 'manual']:
                SensorData.objects.create(estado_alarme=estado)
                mensagem= f" O alarme foi acionado no estado {estado}."
                enviar_em_thread(CHAT_ID, mensagem)
                return JsonResponse({'status': 'sucesso'}, status=201)
            else:
                return JsonResponse({'erro': 'dados inválidos'}, status=400)
        except Exception as e:
            return JsonResponse({'erro': str(e)}, status=500)

    return JsonResponse({'erro': 'método não permitido'}, status=405)


def obter_estado_alarme(request):
    if request.method == 'GET':
        estado = ComandoParaESP.objects.order_by('-id').last()
        if estado:
            return JsonResponse({
                'estado': estado.modo_desejado,
                'atualizado_em': estado.atualizado_em.isoformat()
            })
        else:
            return JsonResponse({'estado': 'indefinido', 'mensagem': 'Nenhum estado registrado ainda'})
    else:
        return JsonResponse({'erro': 'método não permitido'}, status=405)


def verificar_buzzer(request):
    if request.method == 'GET':
        cmd = ComandoParaESP.objects.order_by('-id').last()
        if cmd:
            return JsonResponse({'disparar_buzzer': cmd.disparar_buzzer})
        else:
            return JsonResponse({'disparar_buzzer': False})
        


@csrf_exempt
def resetar_buzzer(request):
    if request.method == 'POST':
        cmd = ComandoParaESP.objects.order_by('-id').last()
        if cmd:
            cmd.disparar_buzzer = False
            cmd.atualizado_em = timezone.now()
            cmd.save()
            return JsonResponse({'status': 'buzzer resetado'})
        else:
            return JsonResponse({'erro': 'nenhum comando encontrado'}, status=404)
    else:
        return JsonResponse({'erro': 'método não permitido'}, status=405)

