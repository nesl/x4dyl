sleep 10s
for i in 026 027 028
do
	play "./sample_speech/p246_"$i".wav" &
	./Runme > "p246_"$i"_1500_50.txt" &
	sleep 12s &
	wait
done
	
