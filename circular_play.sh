sleep 10s
for i in 100 200 300 400 500 600
do
	play "./static_freq/"$i"Hz.wav" &
	./Runme > "freq_"$i"_100cm_1500Hz_4.txt" &
	sleep 7s &
	wait
done
	
